#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Mei Tao
"""cam-snap: S5KC505A front-camera snapshot with the vendor (Qualcomm chromatix) tuning applied.

Pipeline: RAW10 -> black level -> lens shading (4 illuminant meshes) -> Malvar-He-Cutler demosaic
-> white balance -> vendor CCM -> vendor gamma -> unsharp -> rotate 180 -> JPEG/PNG.
User-space only; touches no system files."""
import sys, os, json, time, glob, argparse, subprocess, re, shlex, tempfile
import numpy as np
D = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, D)   # wrapper runs python -I; make sibling modules (denoise) importable
TUN = json.load(open(os.path.join(D, 's5kc505a_tuning.json')))
BL = 63.75   # vendor linearization pivot 255/4096 (12-bit) = 63.75 (10-bit); measured 63.75 @1x, ~63.2 @16x
RES_PATH = os.path.join(D, 'residual.json')
LUMA_ALPHA = 0.5   # 0 = keep vendor vignetting, 1 = fully flat (more corner noise)
def load_residual():
    if os.environ.get('CAM_NO_RESIDUAL') or not os.path.exists(RES_PATH): return None
    r = json.load(open(RES_PATH))
    m = {k: np.array(r[k], np.float32).reshape(13, 17) for k in ('R', 'Gr', 'Gb', 'B')}
    # chroma shading corrected fully; luminance (vignetting) only by LUMA_ALPHA (vendor tuning keeps some on purpose)
    alpha = float(os.environ.get('CAM_FLAT_LUMA', LUMA_ALPHA))
    L = (m['Gr'] + m['Gb']) / 2
    return {k: (v / L) * L ** alpha for k, v in m.items()}
RES = load_residual()
SENSOR_NAME = 's5kc505a'

# ------------------------------------------------------------------ device discovery / capture
def sh(cmd, check=True, timeout=120):
    r = subprocess.run(cmd, shell=True, capture_output=True, timeout=timeout)
    if check and r.returncode != 0:
        raise RuntimeError('cmd failed: %s\n%s' % (cmd, r.stderr.decode(errors='replace')[-400:]))
    return r.stdout.decode(errors='replace')

def find_media():
    for m in sorted(glob.glob('/dev/media*')):
        if 'qcom-camss' in sh('media-ctl -d %s -p 2>/dev/null | head -5' % m, check=False): return m
    raise RuntimeError('qcom-camss media device not found')

def find_sensor_subdev():
    for n in glob.glob('/sys/class/video4linux/v4l-subdev*'):
        if open(n + '/name').read().startswith(SENSOR_NAME): return '/dev/' + os.path.basename(n)
    raise RuntimeError('sensor subdev not found (is the s5kc505a module loaded?)')

def find_video():
    for n in glob.glob('/sys/class/video4linux/video*'):
        if open(n + '/name').read().strip() == 'msm_vfe0_video0': return '/dev/' + os.path.basename(n)
    raise RuntimeError('msm_vfe0_video0 not found')

def ensure_module(full):
    p = '/sys/module/s5kc505a/parameters/full_res'
    cur = open(p).read().strip() if os.path.exists(p) else None
    want = 'Y' if full else cur
    if cur is None or (full and cur != 'Y'):
        sh('rmmod s5kc505a 2>/dev/null; modprobe s5kc505a %s' % ('full_res=1' if full else ''), check=False)
        time.sleep(1.5)

def reset_flips():
    # The patched driver can mirror in the sensor, which changes the Bayer order. cam-snap
    # always captures unflipped GRBG and rotates in software, so force both flips off.
    sd = find_sensor_subdev()
    if 'horizontal_flip' in sh('v4l2-ctl -d %s -l' % sd, check=False):
        sh('v4l2-ctl -d %s --set-ctrl horizontal_flip=0,vertical_flip=0' % sd)

def setup_pipeline(media, W, H):
    reset_flips()
    sh('media-ctl -d %s -r' % media)
    sh("media-ctl -d %s -l '\"msm_csiphy0\":1->\"msm_csid0\":0[1]'" % media)
    sh("media-ctl -d %s -l '\"msm_csid0\":1->\"msm_ispif0\":0[1]'" % media)
    sh("media-ctl -d %s -l '\"msm_ispif0\":1->\"msm_vfe0_rdi0\":0[1]'" % media)
    fmt = 'SGRBG10_1X10/%dx%d' % (W, H)
    entity = open('/sys/class/video4linux/' + os.path.basename(find_sensor_subdev()) + '/name').read().strip()
    sh('media-ctl -d %s -V %s' % (media, shlex.quote('"%s":0[fmt:%s]' % (entity, fmt))))
    for e in ('msm_csiphy0', 'msm_csid0', 'msm_ispif0', 'msm_vfe0_rdi0'):
        sh('media-ctl -d %s -V \'"%s":0[fmt:%s field:none]\'' % (media, e, fmt))

def capture_raw(video, W, H, skip, path, count=1):
    sh('v4l2-ctl -d %s --set-fmt-video=width=%d,height=%d,pixelformat=pgAA --stream-mmap=4 --stream-skip=%d --stream-count=%d --stream-to=%s' % (video, W, H, skip, count, shlex.quote(path)), timeout=180)
    data = open(path, 'rb').read(); os.unlink(path)
    bpl = len(data) // (H * count)
    return data, bpl

def unpack(data, W, H, bpl):
    r = np.frombuffer(data[:bpl * H], np.uint8).reshape(H, bpl)[:, :W * 5 // 4].reshape(H, W // 4, 5).astype(np.uint16)
    p = np.zeros((H, W), np.float32)
    for k in range(4): p[:, k::4] = (r[:, :, k] << 2) | ((r[:, :, 4] >> (2 * k)) & 3)
    return p

# ------------------------------------------------------------------ simple AE (50 Hz anti-banding)
EXP_MIN, EXP_MAX, G_MIN, G_MAX = 4, 2022, 32, 512
BAND = 609   # 10 ms in sensor lines (33.3 ms / 2030 lines per frame)
def split_sensitivity(T):
    T = max(EXP_MIN, min(T, EXP_MAX * G_MAX / G_MIN))
    if T <= EXP_MAX:
        e = T if T < BAND else int(T // BAND) * BAND
        g = T / e * G_MIN
    else:
        e = EXP_MAX // BAND * BAND; g = T / e * G_MIN
    return int(max(EXP_MIN, min(e, EXP_MAX))), int(max(G_MIN, min(round(g), G_MAX)))

def green_mean(p):
    H, W = p.shape; c = p[H // 4:3 * H // 4, W // 4:3 * W // 4]
    return max(float((c[0::2, 0::2].mean() + c[1::2, 1::2].mean()) / 2 - BL), 1.0)

# ------------------------------------------------------------------ ISP stages
POW = 4
REF = {k: tuple(v) for k, v in TUN['awb_ref'].items()}
def illum_weights(rg, bg):
    w = {n: 1.0 / (np.hypot(rg - r, bg - b) + 1e-4) ** POW for n, (r, b) in REF.items()}
    s = sum(w.values()); return {n: v / s for n, v in w.items()}

def awb_measure(rgb, tol=0.07):
    y = rgb @ np.array([0.3, 0.59, 0.11], np.float32); lo, hi = np.percentile(y, [20, 96]); g = np.maximum(rgb[..., 1], 1e-4)
    rg = rgb[..., 0] / g; bg = rgb[..., 2] / g; m = (y > lo) & (y < hi) & (rgb.max(-1) < 0.95)
    d = np.full(rg.shape, 9.0, np.float32)
    for r, b in REF.values(): d = np.minimum(d, np.hypot(rg - r, bg - b))
    sel = m & (d < tol)
    s = rgb[sel].mean(0) if sel.mean() > 0.03 else rgb[m].mean(0)
    return float(s[0] / s[1]), float(s[2] / s[1])

def mesh_for(w):
    g = {'TL84': w['TL84'] + w['CW'] + w['CustFlo'], 'A': w['A'], 'D65': w['D65'] + w['D75'] + w['D50'], 'H': w['H']}
    out = [np.zeros((13, 17), np.float32) for _ in range(4)]
    for n, wt in g.items():
        for c, k in enumerate(('r_gain', 'gr_gain', 'gb_gain', 'b_gain')):
            out[c] += wt * np.array(TUN['lsc'][n][k], np.float32).reshape(13, 17)
    return out

def interp_plane(mesh, H, W, dy, dx):
    Hs, Ws = H // 2, W // 2
    ys = (2 * np.arange(Hs) + dy) / (H - 1) * 12; xs = (2 * np.arange(Ws) + dx) / (W - 1) * 16
    x0 = np.minimum(np.floor(xs).astype(int), 15); fx = xs - x0
    A = mesh[:, x0] * (1 - fx) + mesh[:, x0 + 1] * fx
    y0 = np.minimum(np.floor(ys).astype(int), 11); fy = (ys - y0)[:, None]
    return (A[y0] * (1 - fy) + A[y0 + 1] * fy).astype(np.float32)

def apply_lsc(p, w, use_residual=True):
    H, W = p.shape; R, Gr, Gb, B = mesh_for(w); q = p.copy()
    if use_residual and RES is not None: R, Gr, Gb, B = R * RES['R'], Gr * RES['Gr'], Gb * RES['Gb'], B * RES['B']
    q[0::2, 0::2] *= interp_plane(Gr, H, W, 0, 0); q[0::2, 1::2] *= interp_plane(R, H, W, 0, 1)
    q[1::2, 0::2] *= interp_plane(B, H, W, 1, 0);  q[1::2, 1::2] *= interp_plane(Gb, H, W, 1, 1)
    return q

def conv(x, k):
    kh, kw = k.shape; ph, pw = kh // 2, kw // 2; xp = np.pad(x, ((ph, ph), (pw, pw)), mode='reflect'); out = np.zeros_like(x)
    for i in range(kh):
        for j in range(kw):
            if k[i, j] != 0: out += k[i, j] * xp[i:i + x.shape[0], j:j + x.shape[1]]
    return out
KG = np.array([[0,0,-1,0,0],[0,0,2,0,0],[-1,2,4,2,-1],[0,0,2,0,0],[0,0,-1,0,0]], np.float32) / 8
KH = np.array([[0,0,.5,0,0],[0,-1,0,-1,0],[-1,4,5,4,-1],[0,-1,0,-1,0],[0,0,.5,0,0]], np.float32) / 8
KV = KH.T.copy()
KX = np.array([[0,0,-1.5,0,0],[0,2,0,2,0],[-1.5,0,6,0,-1.5],[0,2,0,2,0],[0,0,-1.5,0,0]], np.float32) / 8

def demosaic_mhc(p):   # GRBG
    H, W = p.shape; yy, xx = np.mgrid[0:H, 0:W]; ye = (yy % 2 == 0); xe = (xx % 2 == 0)
    isR = ye & ~xe; isB = ~ye & xe; isGr = ye & xe; isGb = ~ye & ~xe
    g = conv(p, KG); h = conv(p, KH); v = conv(p, KV); x = conv(p, KX)
    G = np.where(isR | isB, g, p)
    R = np.where(isR, p, np.where(isGr, h, np.where(isGb, v, x)))
    B = np.where(isB, p, np.where(isGb, h, np.where(isGr, v, x)))
    return np.stack([R, G, B], -1)

CCMS = {k: np.array(v, np.float32).reshape(3, 3) for k, v in TUN['ccm'].items()}
def ccm_mix(w):
    g = {'TL84': w['TL84'] + w['CW'] + w['CustFlo'], 'A': w['A'], 'D65': w['D65'] + w['D75'], 'D50': w['D50'], 'H': w['H']}
    return sum(g[n] * CCMS[n] for n in g)

GLUT = [np.array(TUN['gamma14'][k], np.float32) / 16383.0 for k in 'RGB']
XG = np.linspace(0, 1, 512, dtype=np.float32)
def gamma_vendor(rgb): return np.stack([np.interp(rgb[..., c], XG, GLUT[c]) for c in range(3)], -1)

def sharpen(img, amount, thr=0.004):
    # unsharp mask on luma with a small coring threshold so residual noise is not re-amplified
    k = np.array([[1,2,1],[2,4,2],[1,2,1]], np.float32) / 16; y = img @ np.array([0.299, 0.587, 0.114], np.float32); hp = y - conv(y, k)
    hp = np.sign(hp) * np.maximum(np.abs(hp) - thr, 0)
    return np.clip(img + (hp * amount)[..., None], 0, 1)

def interp_rows(mesh, H, W, dy, dx, r0, r1):
    """Bilinear mesh gains for sub-plane rows r0..r1 (same mapping as interp_plane)."""
    Ws = W // 2
    ys = (2 * np.arange(r0, r1) + dy) / (H - 1) * 12; xs = (2 * np.arange(Ws) + dx) / (W - 1) * 16
    x0 = np.minimum(np.floor(xs).astype(int), 15); fx = xs - x0
    A = mesh[:, x0] * (1 - fx) + mesh[:, x0 + 1] * fx
    y0 = np.minimum(np.floor(ys).astype(int), 11); fy = (ys - y0)[:, None]
    return (A[y0] * (1 - fy) + A[y0 + 1] * fy).astype(np.float32)

def process(p, target_luma=0.36, sharp=0.6, log=print, dn_luma=1.0, dn_chroma=1.0, half=False, band=256):
    """RAW10 -> gamma-encoded RGB, processed in horizontal bands to keep memory low (2 GB device).
    half=True returns the 2x2-downscaled result (used for full-res capture)."""
    H, W = p.shape
    x = (p - BL) / (1023.0 - BL)        # keep negative noise until the end (clipping early tints shadows)
    q = np.stack([x[0::2, 1::2], (x[0::2, 0::2] + x[1::2, 1::2]) / 2, x[1::2, 0::2]], -1)
    rg, bg = awb_measure(q); w = illum_weights(rg, bg)
    cct = sum(w[n] * TUN['cct'][n] for n in w)
    log('AWB R/G=%.3f B/G=%.3f  CCT~%dK (%s)' % (rg, bg, cct, max(w, key=w.get)))
    R, Gr, Gb, B = mesh_for(w)
    if RES is not None: R, Gr, Gb, B = R * RES['R'], Gr * RES['Gr'], Gb * RES['Gb'], B * RES['B']
    gains = np.array([1 / rg, 1.0, 1 / bg], np.float32); M = ccm_mix(w).astype(np.float32)
    Hs = H // 2   # exposure normalisation (stand-in for AE) on the coarse half-res image
    lq = np.stack([interp_rows(R, H, W, 0, 1, 0, Hs), (interp_rows(Gr, H, W, 0, 0, 0, Hs) + interp_rows(Gb, H, W, 1, 1, 0, Hs)) / 2,
                   interp_rows(B, H, W, 1, 0, 0, Hs)], -1)
    qc = ((q * lq)[::2, ::2] * gains) @ M.T; del lq, q
    lo, hi = 0.1, 300.0
    for _ in range(16):
        s = (lo * hi) ** 0.5; m = (gamma_vendor(np.clip(qc * s, 0, 1)) @ np.array([0.299, 0.587, 0.114], np.float32)).mean()
        if m < target_luma: lo = s
        else: hi = s
    del qc
    oh, ow = (H // 2, W // 2) if half else (H, W)
    out = np.empty((oh, ow, 3), np.float32); ov = 4
    for y0 in range(0, H, band):
        a, b = max(0, y0 - ov), min(H, y0 + band + ov)
        xb = x[a:b].copy(); r0, r1 = a // 2, b // 2
        xb[0::2, 0::2] *= interp_rows(Gr, H, W, 0, 0, r0, r1); xb[0::2, 1::2] *= interp_rows(R, H, W, 0, 1, r0, r1)
        xb[1::2, 0::2] *= interp_rows(B, H, W, 1, 0, r0, r1);  xb[1::2, 1::2] *= interp_rows(Gb, H, W, 1, 1, r0, r1)
        n = min(band, H - y0)
        rgb = demosaic_mhc(xb)[y0 - a:y0 - a + n]; del xb
        o = gamma_vendor(np.clip(((rgb * gains) @ M.T) * s, 0, 1)); del rgb
        if half: out[y0 // 2:(y0 + n) // 2] = down2(o)
        else: out[y0:y0 + n] = o
    del x
    if dn_luma > 0 or dn_chroma > 0:
        import denoise
        out = denoise.denoise_tiled(out, dn_luma, dn_chroma, log=log)
    return sharpen(out, sharp) if sharp > 0 else out

def to8(a): return (np.clip(a, 0, 1) * 255 + 0.5).astype(np.uint8)
def down2(a): return (a[0::2, 0::2] + a[1::2, 0::2] + a[0::2, 1::2] + a[1::2, 1::2]) / 4

def write_image(path, a8):
    ppm = b'P6\n%d %d\n255\n' % (a8.shape[1], a8.shape[0]) + a8.tobytes()
    if path.lower().endswith('.ppm'): open(path, 'wb').write(ppm); return
    q = ['-q:v', '2'] if path.lower().endswith(('.jpg', '.jpeg')) else []
    r = subprocess.run(['ffmpeg', '-loglevel', 'error', '-y', '-f', 'image2pipe', '-vcodec', 'ppm', '-i', '-'] + q + [os.path.abspath(path)], input=ppm, capture_output=True)
    if r.returncode: raise RuntimeError(r.stderr.decode()[-300:])

def main():
    ap = argparse.ArgumentParser(description=__doc__.split(chr(10))[0])
    ap.add_argument('out', help='output file (.jpg/.png/.ppm)')
    ap.add_argument('--full', action='store_true', help='capture 2608x1960 and downscale 2x (sharper, slower); loads s5kc505a with full_res=1')
    ap.add_argument('--native', action='store_true', help='with --full: keep 2608x1960 output (no downscale)')
    ap.add_argument('--no-ae', action='store_true', help='keep current exposure/gain')
    ap.add_argument('--no-rotate', action='store_true')
    ap.add_argument('--sharp', type=float, default=0.6)
    ap.add_argument('--denoise', type=float, default=1.0, help='denoise strength (0 = off, 1 = default, 1.6 = strong); chroma uses the same factor')
    ap.add_argument('--target', type=float, default=86.0, help='AE target: mean green level in DN above black (10-bit)')
    ap.add_argument('--no-residual', action='store_true', help='ignore residual.json (per-unit flat-field correction)')
    ap.add_argument('--raw', help='also save the unprocessed RAW10 here')
    a = ap.parse_args(); t0 = time.time()
    if a.no_residual: globals()['RES'] = None
    log = lambda s: print('[%5.1fs] %s' % (time.time() - t0, s), flush=True)
    ensure_module(a.full); media = find_media(); sd = find_sensor_subdev(); vid = find_video()
    tmpdir = tempfile.TemporaryDirectory(prefix='camsnap-')
    tmp = os.path.join(tmpdir.name, 'capture.raw')
    # AE on a quick binned capture
    if not a.no_ae:
        setup_pipeline(media, 1304, 980)
        T = 1800.0 * 128 / 32
        for i in range(4):
            e, g = split_sensitivity(T); sh('v4l2-ctl -d %s --set-ctrl exposure=%d,analogue_gain=%d' % (sd, e, g))
            data, bpl = capture_raw(vid, 1304, 980, 6, tmp); m = green_mean(unpack(data, 1304, 980, bpl))
            log('AE iter %d: exposure=%d gain=%d -> green mean %.1f DN' % (i, e, g, m))
            if 0.8 < m / a.target < 1.25: break
            T *= a.target / m
    W, H = (2608, 1960) if a.full else (1304, 980)
    setup_pipeline(media, W, H); data, bpl = capture_raw(vid, W, H, 6, tmp)
    if a.raw: open(a.raw, 'wb').write(data)
    log('captured %dx%d RAW (%d bytes)' % (W, H, len(data)))
    p = unpack(data, W, H, bpl); fulldown = a.full and not a.native
    img = process(p, sharp=a.sharp, log=log, dn_luma=a.denoise, dn_chroma=a.denoise, half=fulldown)
    del p
    if not a.no_rotate: img = img[::-1, ::-1]
    write_image(a.out, to8(img)); sh('media-ctl -d %s -r' % media, check=False)
    log('wrote %s' % a.out)

if __name__ == '__main__':
    main()
