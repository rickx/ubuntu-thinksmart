#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Mei Tao
"""cam-flat: per-unit flat-field calibration on top of the vendor lens-shading tables.

Point the camera at an evenly lit white sheet (fill the whole view, no shadows, no light source in view), then run:
    cam-flat            # measure + show result only
    cam-flat --save     # also write residual.json (picked up automatically by cam-snap)
    rm residual.json    # undo"""
import sys, os, json, time, argparse, tempfile
import numpy as np
D = os.path.dirname(os.path.abspath(__file__)); sys.path.insert(0, D)
import cam_snap as C

def grid_means(plane, H2, W2):
    # mean of a Bayer sub-plane around each of the 17x13 mesh nodes (window = +-half cell, robust to hot pixels)
    out = np.zeros((13, 17), np.float32); hh = max(2, H2 // 24); hw = max(2, W2 // 32)
    for j in range(13):
        cy = int(round(j * (H2 - 1) / 12)); y0, y1 = max(0, cy - hh), min(H2, cy + hh + 1)
        for i in range(17):
            cx = int(round(i * (W2 - 1) / 16)); x0, x1 = max(0, cx - hw), min(W2, cx + hw + 1)
            out[j, i] = np.median(plane[y0:y1, x0:x1])
    return out

def main():
    ap = argparse.ArgumentParser(description=__doc__.split(chr(10))[0])
    ap.add_argument('--save', action='store_true'); ap.add_argument('--frames', type=int, default=16)
    ap.add_argument('--target', type=float, default=500.0, help='green DN above black for the flat (higher = less noise, must not clip)')
    ap.add_argument('--out', default=C.RES_PATH); a = ap.parse_args(); t0 = time.time()
    log = lambda s: print('[%5.1fs] %s' % (time.time() - t0, s), flush=True)
    C.ensure_module(False); media = C.find_media(); sd = C.find_sensor_subdev(); vid = C.find_video(); W, H = 1304, 980
    tmpdir = tempfile.TemporaryDirectory(prefix='camflat-')
    C.setup_pipeline(media, W, H); tmp = os.path.join(tmpdir.name, 'capture.raw'); T = 1800.0 * 128 / 32
    for i in range(6):
        e, g = C.split_sensitivity(T); C.sh('v4l2-ctl -d %s --set-ctrl exposure=%d,analogue_gain=%d' % (sd, e, g))
        data, bpl = C.capture_raw(vid, W, H, 6, tmp); p = C.unpack(data, W, H, bpl); m = C.green_mean(p)
        mx = float(np.percentile(p, 99.8)); log('AE %d: exposure=%d gain=%d -> green %.0f DN, p99.8=%.0f' % (i, e, g, m, mx))
        if mx > 960: T *= 0.6; continue
        if 0.85 < m / a.target < 1.15: break
        T *= min(a.target / m, 3.0)
    if mx > 980: log('WARNING: image is clipping, reduce light');
    data, bpl = C.capture_raw(vid, W, H, 4, tmp, count=a.frames); fs = len(data) // a.frames
    acc = np.zeros((H, W), np.float64)
    for k in range(a.frames): acc += C.unpack(data[k * fs:(k + 1) * fs], W, H, bpl)
    p = (acc / a.frames).astype(np.float32); log('averaged %d frames' % a.frames)
    x = np.clip(p - C.BL, 0, None) / (1023.0 - C.BL)
    q = np.stack([x[0::2, 1::2], (x[0::2, 0::2] + x[1::2, 1::2]) / 2, x[1::2, 0::2]], -1)
    rg = float(q[..., 0].mean() / q[..., 1].mean()); bg = float(q[..., 2].mean() / q[..., 1].mean())
    w = C.illum_weights(rg, bg); cct = sum(w[n] * C.TUN['cct'][n] for n in w)
    log('white sheet: R/G=%.3f B/G=%.3f  CCT~%dK (%s)' % (rg, bg, cct, max(w, key=w.get)))
    xl = C.apply_lsc(x, w, use_residual=False)           # vendor tables only
    H2, W2 = H // 2, W // 2
    planes = {'Gr': xl[0::2, 0::2], 'R': xl[0::2, 1::2], 'B': xl[1::2, 0::2], 'Gb': xl[1::2, 1::2]}
    meshes = {k: grid_means(v, H2, W2) for k, v in planes.items()}
    cen = {k: float(np.mean(m[5:8, 7:10])) for k, m in meshes.items()}
    rel = {k: meshes[k] / cen[k] for k in meshes}                 # 1.0 = perfectly flat
    lum = (rel['Gr'] + rel['Gb']) / 2
    log('after VENDOR tables only: luminance (G) corner/centre = %.3f / %.3f / %.3f / %.3f ; worst |dev|=%.1f%%' % (lum[0, 0], lum[0, -1], lum[-1, 0], lum[-1, -1], 100 * np.abs(lum - 1).max()))
    for k in ('R', 'B'):
        cr = rel[k] / lum; log('  %s/G chroma deviation: min %.3f max %.3f (1.000 = neutral)' % (k, cr.min(), cr.max()))
    # sanity: the sheet must be flat
    raw_lum = grid_means((p[0::2, 0::2] + p[1::2, 1::2]) / 2 - C.BL, H2, W2); raw_lum /= raw_lum[5:8, 7:10].mean()
    tex = float(np.std(np.diff(lum, axis=1)))
    if raw_lum.min() < 0.12 or tex > 0.05: log('WARNING: target does not look like an evenly lit flat sheet (edge level %.2f, mesh roughness %.3f) - result unreliable' % (raw_lum.min(), tex))
    res = {k: np.clip(1.0 / rel[k], 0.7, 1.5) for k in rel}
    for k in res:   # light 3x3 smoothing to avoid imprinting paper texture
        r = np.pad(res[k], 1, mode='edge'); res[k] = sum(r[i:i + 13, j:j + 17] for i in range(3) for j in range(3)) / 9
    # keep the sheet's own colour at centre neutral: residual is defined relative to centre so AWB is unaffected
    for k in res: res[k] = res[k] / float(np.mean(res[k][5:8, 7:10]))
    after = {k: rel[k] * res[k] for k in rel}; lum2 = (after['Gr'] + after['Gb']) / 2
    log('with residual: worst luminance |dev|=%.1f%% ; R/G dev %.3f..%.3f ; B/G dev %.3f..%.3f' % (100 * np.abs(lum2 - 1).max(), (after['R'] / lum2).min(), (after['R'] / lum2).max(), (after['B'] / lum2).min(), (after['B'] / lum2).max()))
    log('residual gain range: ' + ', '.join('%s %.2f..%.2f' % (k, res[k].min(), res[k].max()) for k in res))
    out = {'created': time.strftime('%Y-%m-%d %H:%M:%S'), 'illuminant_cct': cct, 'rg': rg, 'bg': bg, 'frames': a.frames,
           **{k: [round(float(v), 5) for v in res[k].ravel()] for k in res}}
    if a.save: json.dump(out, open(a.out, 'w')); log('saved %s' % a.out)
    else: log('not saved (use --save)')
    C.sh('media-ctl -d %s -r' % media, check=False)

if __name__ == '__main__': main()
