# S5KC505A reference tuning and flat-field calibration

SPDX-License-Identifier: CC-BY-4.0

## Recovering reference values

The reference JSON identifies Lenovo CD-18781Y firmware 210228.109,
`libchromatix_s5kc505a_*.so` and chromatix format `0x0309`. These are
decoded values from proprietary firmware, not an open vendor ISP library.
See [NOTICE.md](../../../sources/camera/NOTICE.md); maintainers must review
whether to distribute those numeric data. No vendor binary or confidential
calibration PDF is included in this contribution.

To reproduce decoding, inspect the ELF and disassemble `load_chromatix` to
locate the returned structure. Translate virtual addresses through the ELF
section/program mapping to file offsets. Do not call the library. Match
the structure to the **0x0309 msm8953 mm-camerasdk headers**, respecting
32-bit pointer sizes, alignment, embedded arrays and relocation targets.
The version number alone does not justify applying another SDK's offsets.
Use matching structure field offsets, rather than searching for plausible
floats. No universal byte-offset map or extraction program is supplied here.

Sanity-check the resulting data against table dimensions, channel order,
finite gain ranges and monotonic gamma values. Keep the decoded JSON with
its firmware/version metadata. The current data contain 13 rows x 17 columns
per R/Gr/Gb/B shading table, five 3x3 CCMs, 14-bit gamma entries, black level
64 on a 10-bit scale, and illuminant AWB reference R/G and B/G ratios.
Those illuminant ratios describe expected unbalanced sensor responses; they
are not gains to apply directly or per-module OTP calibration values.

## What each block represents

LSC multiplies the black-subtracted mosaic signal to correct field-dependent
luminance and color shading. Reference sets are H (2300 K), A (2850 K),
TL84 (4000 K) and D65 (6500 K). Gr and Gb are independent in the vendor
tables. A warm edge cast can remain even when green luminance looks flat;
inspect normalized R/G and B/G across the field as well as corner brightness.

CCM is a row-major 3x3 linear RGB transform, applied after white balance and
before gamma. H/A/TL84/D50/D65 reference sets use 2300/2850/4000/5000/6500 K.
Do not transpose them or treat their coefficients as sRGB-space adjustments.
The offline path mixes references using measured chromaticity proximity;
libcamera interpolates neighboring sets linearly in Kelvin. They are
different illuminant-estimation models, so identical reference tables do not
guarantee identical rendering.

The offline renderer subtracts 63.75 DN, interpolates the reference 14-bit
gamma and applies optional guided denoise and sharpening. The SoftISP tuning
uses black level 4096 in the helper/tuning's 16-bit scale (approximately
64/1023), and its Adjust stage's own gamma/contrast behavior. The reference
gamma JSON is not implicitly consumed by Adjust. Likewise, SoftISP's
gray-world AWB does not consume the vendor AWB reference dictionary.

## Conversion to libcamera 0.8

`sources/camera/tools/make_libcamera_tuning.py` emits Ccm/Lsc sections.
Ccm accepts `ccms: [{ct: <Kelvin>, ccm: [9 row-major floats]}]`. Lsc accepts
`type: table` and `sets: [{ct: <Kelvin>, r: [289 bytes], g: [289 bytes],
b: [289 bytes]}]`. The keys belong under their respective algorithm entries
in the top-level `algorithms` list; these sections are not a complete file.

For LSC, resample 13x17 to 17x17, averaging Gr/Gb into the shared green plane.
Quantize nominal gain as `round(gain*64)` and clamp to 0..255. Flatten rows
left-to-right, top-to-bottom. The sensor's default H+V flips for rotation=180
require reversing each flattened grid. For native unflipped GRBG captures,
use `--no-rot180`. Both orientations must be checked against the active
sensor controls, not inferred solely from output JPEG rotation.

The renderer's exact scale is normalized RGBA8 multiplied by 4, hence
`byte*4/255`: a nominal unity byte 64 is actually 256/255. Bilinear sampling
uses texel coordinates `clamp(17*u-0.5, 0, 16)`, not `16*u`. This existing
shader convention differs slightly from treating quantized nodes as exact
gain/64 samples; the statistics patch follows the renderer exactly. Large
corner gains can saturate the representable table and amplify noise.

The generic committed YAML was generated without residual/OTP input. Its
AGC target starts at 0.12 and retains BlackLevel, Awb and Adjust. The image
quality numbers below were measured using an additional per-unit residual;
they are not evidence that generic tables calibrate every module equally.

## Per-unit flat-field workflow

Use a uniformly illuminated diffuse field filling the sensor view. Avoid a
direct light source, gradients, shadows, flicker and clipping. Lock the mode,
orientation, exposure and gain; acquire and average multiple frames. The
generic Samsung training material suggests a midscale green signal under
approximately 5100 K illumination, but does not specify this module's OTP
packing. The included `cam-flat --target 300` is an accessible starting point.

The tool subtracts black, applies reference shading without an old residual,
measures robust 13x17 channel-grid values and normalizes the remaining
correction at the centre. Inspect luminance and normalized R/G/B/G maps
before saving. A lens diffuser or paper stack must itself be spatially
uniform; otherwise its defects are calibrated into the result.

Save your own residual with `cam-flat --target 300 --save`. Cam-snap picks it
up locally. Full chroma correction is applied; default luminance exponent
0.5 retains some vignetting to contain noise. `CAM_FLAT_LUMA=1` flattens
luminance fully. Regenerate libcamera's Ccm/Lsc sections and replace those
sections in the complete YAML. Repeat for different illumination and modes;
one residual is a practical approximation, not a validated full calibration.

Delete residual.json to return to generic tuning. Never submit another
unit's residual, OTP dump or captured frame as universal reference data.
In-sensor SRAM loading was tested unsuccessfully; it is not a replacement
for this software flat-field workflow or a confirmed OTP decoder.

## Exposure target evidence

On the reported derivative platform, raw corner luminance was roughly 0.3
of the centre, and edge red R/G about 0.69 of centre. Raw statistics caused
gray-world warmth and encouraged overexposure after LSC. With LSC-aware GPU
statistics, a neutral surface measured R/G 0.999, B/G 0.983, compared with
about 5% warmth before the change.

For one daylight-window room, a 98th-percentile upper constraint let the
bright window dominate, producing mean Y about 31 versus offline capture's
91. Removing that constraint and using target 0.12 produced mean/median Y
84/64, versus 92/66 offline; clipping was 6.8% versus 9.3%. Earlier raw-stats
tuning needed target 0.10. These are scene-specific comparisons, not a
universal calibration or a guarantee of highlight protection. The 0.16
library default is a sensible neighborhood to explore after correcting
statistics; 0.12 was the reported practical choice on this scene.
