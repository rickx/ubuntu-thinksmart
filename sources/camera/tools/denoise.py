# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Mei Tao
"""Edge-preserving denoise for cam-snap (applied in the gamma-encoded YCbCr domain).

Luma: self-guided filter (He et al.) with eps tied to the measured noise level.
Chroma: cross guided filter using luma as guide (stronger, larger radius).
All box filters use integral images, so cost is independent of the radius."""
import numpy as np

def box(x, r):
    """Mean over a (2r+1)^2 window with edge replication."""
    p = np.pad(x, ((r + 1, r), (r + 1, r)), mode='edge').astype(np.float64)
    c = p.cumsum(0).cumsum(1)
    n = 2 * r + 1
    s = c[n:, n:] - c[:-n, n:] - c[n:, :-n] + c[:-n, :-n]
    return (s / (n * n)).astype(np.float32)

def guided(I, p, r, eps):
    mI, mp = box(I, r), box(p, r)
    cov = box(I * p, r) - mI * mp
    var = box(I * I, r) - mI * mI
    a = cov / (var + eps); b = mp - a * mI
    return box(a, r) * I + box(b, r)

def noise_sigma(y):
    """Robust noise estimate from the Laplacian (MAD), in the units of y."""
    lap = 4 * y[1:-1, 1:-1] - y[:-2, 1:-1] - y[2:, 1:-1] - y[1:-1, :-2] - y[1:-1, 2:]
    return float(np.median(np.abs(lap)) / 0.6745 / np.sqrt(20.0))

M = np.array([[0.299, 0.587, 0.114], [-0.168736, -0.331264, 0.5], [0.5, -0.418688, -0.081312]], np.float32)
Mi = np.linalg.inv(M).astype(np.float32)

def denoise(rgb, luma_strength=1.0, chroma_strength=1.0, log=None):
    ycc = rgb @ M.T
    Y, Cb, Cr = ycc[..., 0], ycc[..., 1], ycc[..., 2]
    s = noise_sigma(Y)
    if log: log('denoise: estimated luma noise sigma %.4f (%.1f / 255)' % (s, s * 255))
    if luma_strength > 0:
        Y = guided(Y, Y, 2, (1.6 * s * luma_strength) ** 2 + 1e-6)
    if chroma_strength > 0:
        e = (3.0 * s * chroma_strength) ** 2 + 1e-6
        Cb = guided(Y, Cb, 6, e); Cr = guided(Y, Cr, 6, e)
    return np.clip(np.stack([Y, Cb, Cr], -1) @ Mi.T, 0, 1)

def denoise_tiled(rgb, luma_strength=1.0, chroma_strength=1.0, log=None, band=512, overlap=16):
    """Same as denoise() but processed in horizontal bands to bound memory (the device has 2 GB RAM).
    The noise level is estimated once on the whole (subsampled) image so all bands use the same strength."""
    H = rgb.shape[0]
    if H <= band + 2 * overlap:
        return denoise(rgb, luma_strength, chroma_strength, log)
    y = (rgb[::2, ::2] @ M.T)[..., 0]; s = noise_sigma(y) * 1.0
    if log: log('denoise (tiled): estimated luma noise sigma %.4f (%.1f / 255)' % (s, s * 255))
    out = np.empty_like(rgb)
    for y0 in range(0, H, band):
        a, b = max(0, y0 - overlap), min(H, y0 + band + overlap)
        r = _denoise_sigma(rgb[a:b], s, luma_strength, chroma_strength)
        out[y0:min(H, y0 + band)] = r[y0 - a:y0 - a + min(band, H - y0)]
    return out

def _denoise_sigma(rgb, s, luma_strength, chroma_strength):
    ycc = rgb @ M.T
    Y, Cb, Cr = ycc[..., 0], ycc[..., 1], ycc[..., 2]
    if luma_strength > 0: Y = guided(Y, Y, 2, (1.6 * s * luma_strength) ** 2 + 1e-6)
    if chroma_strength > 0:
        e = (3.0 * s * chroma_strength) ** 2 + 1e-6
        Cb = guided(Y, Cb, 6, e); Cr = guided(Y, Cr, 6, e)
    return np.clip(np.stack([Y, Cb, Cr], -1) @ Mi.T, 0, 1)
