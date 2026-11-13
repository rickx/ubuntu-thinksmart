#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Mei Tao
"""Load the OTP GAS/LSC block (144 x 20-bit) into the sensor SRAM with several candidate bit-packings,
enable the sensor shading block and measure flat-field uniformity. Needs an evenly lit diffuser on the lens."""
import sys, time, subprocess, os, json, itertools
D = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.dirname(D)); sys.path.insert(0, D)
import numpy as np, cam_snap as C
from s5k_i2c import S5K
d = json.load(open(os.path.join(D, 'otp_dump.json'))); pg = {int(k): bytes.fromhex(v['hex']) for k, v in d.items()}
SEG = pg[3][48:64] + b''.join(pg[p] for p in range(4, 9)) + pg[9][:24]
assert len(SEG) == 360
def perm_decode(perm, hi_first):
    out = []
    for g in range(0, 360, 5):
        x = 0
        for k in perm: x = (x << 8) | SEG[g + k]
        a, b = (x >> 20) & 0xFFFFF, x & 0xFFFFF
        out += [a, b] if hi_first else [b, a]
    return out
def stream_decode(endian):
    n = int.from_bytes(SEG, endian)
    return [(n >> (2880 - 20 * (i + 1))) & 0xFFFFF if endian == 'big' else (n >> (20 * i)) & 0xFFFFF for i in range(144)]
CANDS = [('perm(1,0,2,3,4) hi', perm_decode((1, 0, 2, 3, 4), True)), ('perm(1,4,2,3,0) hi', perm_decode((1, 4, 2, 3, 0), True)),
         ('bitstream big-endian', stream_decode('big')), ('bitstream little-endian', stream_decode('little')),
         ('nibble-hdr low-first', [v for g in range(0, 360, 5) for v in (((SEG[g] & 15) << 16) | (SEG[g + 1] << 8) | SEG[g + 2], ((SEG[g] >> 4) << 16) | (SEG[g + 3] << 8) | SEG[g + 4])])]
W, H, BPL = 1304, 980, 1632; FS = BPL * H
media = C.find_media(); sd = C.find_sensor_subdev(); vid = C.find_video()
def grab_stream(n, during=None):
    proc = subprocess.Popen('v4l2-ctl -d %s --set-fmt-video=width=%d,height=%d,pixelformat=pgAA --stream-mmap=4 --stream-count=%d --stream-to=/tmp/sr.raw' % (vid, W, H, n), shell=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    if during: time.sleep(0.8); during()
    proc.wait(); dd = open('/tmp/sr.raw', 'rb').read(); os.unlink('/tmp/sr.raw'); k = len(dd) // FS
    return [C.unpack(dd[i * FS:(i + 1) * FS], W, H, BPL) for i in range(k)]
def uniformity(p):
    x = p - C.BL; ch = {'Gr': x[0::2, 0::2], 'R': x[0::2, 1::2], 'B': x[1::2, 0::2], 'Gb': x[1::2, 1::2]}
    grid = lambda v: np.array([[np.median(v[i * v.shape[0] // 6:(i + 1) * v.shape[0] // 6, j * v.shape[1] // 8:(j + 1) * v.shape[1] // 8]) for j in range(8)] for i in range(6)])
    g = {k: grid(v) for k, v in ch.items()}; G = (g['Gr'] + g['Gb']) / 2; c = G[2:4, 3:5].mean(); L = G / max(c, 1e-6)
    rg = (g['R'] / G) / (g['R'][2:4, 3:5].mean() / G[2:4, 3:5].mean()); bg = (g['B'] / G) / (g['B'][2:4, 3:5].mean() / G[2:4, 3:5].mean())
    return c, L, rg, bg
def report(name, frames):
    p = sum(f.astype(np.float64) for f in frames[-12:]) / len(frames[-12:]); c, L, rg, bg = uniformity(p)
    corners = [L[0, 0], L[0, -1], L[-1, 0], L[-1, -1]]
    print('%-26s centre G=%5.0f | corners %.2f %.2f %.2f %.2f | luma min %.2f max %.2f | R/G %.3f..%.3f | B/G %.3f..%.3f' % (name, c, *corners, L.min(), L.max(), np.nanmin(rg), np.nanmax(rg), np.nanmin(bg), np.nanmax(bg)), flush=True)
C.setup_pipeline(media, W, H)
C.sh('v4l2-ctl -d %s --set-ctrl exposure=1827,analogue_gain=%s' % (sd, sys.argv[1] if len(sys.argv) > 1 else '200'))
report('baseline (sensor LSC off)', grab_stream(20))
for name, vals in CANDS:
    def load(vals=vals):
        s = S5K()
        for addr, v in enumerate(vals):
            s.write8(0x3214, (v >> 16) & 0x0F); s.write8(0x3215, (v >> 8) & 0xFF); s.write8(0x3216, v & 0xFF)
            s.write8(0x3217, addr); s.write8(0x3218, 0x01); s.write8(0x3218, 0x00)
        s.write8(0x0B00, 0x01); s.close()
    t0 = time.time(); fr = grab_stream(45, load); report('SRAM ' + name, fr)
C.sh('media-ctl -d %s -r' % media, check=False)
