#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Mei Tao
"""Convert the decoded vendor (chromatix 0x0309) S5KC505A tuning into libcamera v0.8.0 softisp Ccm/Lsc YAML sections.

libcamera v0.8.0 softisp formats (verified from source by the build worker):
  Ccm: ccms: [{ct: K, ccm: [9 floats row-major]}]   linear interpolation in Kelvin, clamped at ends
  Lsc: type: table, sets: [{ct: K, r: [289], g: [289], b: [289]}]   17x17 nodes at i/16, row-major,
       uint8 values, nominal UQ2.6 (64 = 1x). The GPU actually samples byte*4/255,
       so a constant 64 is 256/255. Only applied on the GPU ISP path.
Vendor LSC is 17 cols x 13 rows with separate Gr/Gb -> resampled to 17x17, G = mean(Gr, Gb)."""
import json, os, sys
import numpy as np
D = os.path.dirname(os.path.abspath(__file__))
T = json.load(open(os.path.join(D, 's5kc505a_tuning.json')))
RES = os.path.join(D, 'residual.json')
res = json.load(open(RES)) if os.path.exists(RES) and '--no-residual' not in sys.argv else None

ROT180 = '--no-rot180' not in sys.argv
LSC_CT = {'H': 2300, 'A': 2850, 'TL84': 4000, 'D65': 6500}
CCM_CT = {'H': 2300, 'A': 2850, 'TL84': 4000, 'D50': 5000, 'D65': 6500}

def resample_13_to_17(m):   # m: (13,17) on rows y=j/12 -> (17,17) on rows y=i/16
    ys = np.arange(17) / 16 * 12; y0 = np.minimum(np.floor(ys).astype(int), 11); fy = (ys - y0)[:, None]
    return m[y0] * (1 - fy) + m[y0 + 1] * fy

clipped = []
def q(m, name):
    v = np.round(m * 64.0)
    if v.max() > 255: clipped.append('%s max gain %.2fx > 3.98x (clipped)' % (name, m.max()))
    return [int(x) for x in np.clip(v, 0, 255).ravel()]

lines = ['  - Lsc:', '      type: table', '      sets:']
for n, ct in sorted(LSC_CT.items(), key=lambda kv: kv[1]):
    t = T['lsc'][n]; get = lambda k: np.array(t[k], np.float32).reshape(13, 17)
    R, Gr, Gb, B = get('r_gain'), get('gr_gain'), get('gb_gain'), get('b_gain')
    if res is not None:
        sys.path.insert(0, D); import cam_snap; rr = cam_snap.load_residual()
        R, Gr, Gb, B = R * rr['R'], Gr * rr['Gr'], Gb * rr['Gb'], B * rr['B']
    G = (Gr + Gb) / 2
    lines.append('        - ct: %d   # vendor %s table' % (ct, n))
    for key, m in (('r', R), ('g', G), ('b', B)):
        vals = q(resample_13_to_17(m), '%s/%s' % (n, key))
        if ROT180: vals = vals[::-1]   # libcamera uses sensor h+v flip for rotation=180 and does not transform LSC tables
        rows = [', '.join('%3d' % x for x in vals[i * 17:(i + 1) * 17]) for i in range(17)]
        lines.append('          %s: [ %s ]' % (key, (',\n               ').join(rows)))
ccm = ['  - Ccm:', '      ccms:']
for n, ct in sorted(CCM_CT.items(), key=lambda kv: kv[1]):
    c = T['ccm'][n]
    ccm.append('        - ct: %d   # vendor %s' % (ct, n))
    ccm.append('          ccm: [ %s,\n                 %s,\n                 %s ]' % tuple(', '.join('%.4f' % x for x in c[r * 3:r * 3 + 3]) for r in range(3)))
out = '\n'.join(ccm + lines) + '\n'
open(os.path.join(D, 'libcamera-ccm-lsc-sections.yaml'), 'w').write(out)
print('wrote libcamera-ccm-lsc-sections.yaml (%d lines), residual %s' % (out.count(chr(10)), 'applied' if res else 'not present'))
for c in clipped: print('NOTE:', c)
