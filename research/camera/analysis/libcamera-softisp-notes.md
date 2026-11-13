# Libcamera GPU SoftISP on MSM8953

SPDX-License-Identifier: CC-BY-4.0

## Platform and upstream baseline

The reported working real-time stack is libcamera v0.8.0 with two local
changes, qcom-camss, S5KC505A and Mesa freedreno FD506 (Adreno 506). Binned
1304x980 streaming reached about 30 fps on ThinkSmart Clean 2026.11.01,
Debian trixie and kernel 6.19.5-thinksmart+ without KCFI. This is reported
device testing of the derivative image, not this contribution's runtime
validation on the upstream Ubuntu 24.04 image.

GPU debayer arrived in libcamera v0.7 behind `softisp-gpu`, using surfaceless
EGL. LSC arrived in v0.8 and applies only to the GPU renderer. The IPA is
`softisp`, pipeline `simple`. V0.8 algorithms are BlackLevel, Agc, Awb, Ccm,
Lsc and Adjust. Older Debian libcamera 0.4 lacks this complete stack. Keep
the newer SONAME, IPA and tuning in a separate prefix with an environment
wrapper; replacing only the library underneath old applications is unsafe.

The mail-ready series in `sources/patches/libcamera/` targets upstream master
be7e83e91348250c015aad6d6f59f1468555bae8. It preserves the behavior of the
tested v0.8 changes while passing fresh cross builds and focused QEMU tests.
CPU fallback still streams raw-statistics/no-LSC output and cannot validate
the GPU tuning; confirm the selected renderer in runtime logs.

## Sensor helper and tunable exposure

The S5KC505A helper registers the exact model name `s5kc505a`,
`AnalogueGainLinear{1, 0, 0, 32}` and black level 4096. Measured analog
codes 0x20..0x200 map to code/32. The black-level scale corresponds to
approximately 64 at 10-bit. No cell-size or control-delay properties were
invented. A helper selects AgcMeanLuminance instead of the fallback AgcMSV,
enabling its tuning node and EV compensation.

`relativeLuminanceTarget` belongs directly below `- Agc:`. It defaults to
0.16; no highlight constraint is needed for a minimal 0.12 starting point.
The shared algorithm also understands exposure/constraint mode tuning, but
invented keys such as a top-level yTarget will not change this path. Exposure
and gain remain subject to sensor control ranges and the active mode.

## Why statistics must follow lens shading

Upstream SwStatsCpu gathers sparse mosaic statistics before LSC. AWB's
channel sums and AGC's Y histogram therefore represented a dark-corner lens
instead of the corrected image. On this lens, corner luminance was about
0.3x centre and red shading caused a scene-dependent warm cast after AWB.
The fix corrects sampled Bayer values with the same active-frame LSC table
before either consumer uses them, while keeping them before AWB and CCM.

The per-frame DebayerParams gains an enabled flag, written on every IPA
prepare even when the CT/table version does not change. GPU processing
passes that frame's parameters to SwStatsCpu immediately before statistics.
The CPU path explicitly marks LSC unavailable because its renderer does not
apply it. Disabled LSC keeps existing statistics unchanged.

A gain map caches four channel gains per sampled Bayer cell. The map uses
the exact shader field: bilinear/clamp-to-edge 17x17 RGB texture, normalized
byte*4/255, and the existing stride-aware input coordinates. Each component
is evaluated at its own sample centre. There is no extra GPU flip; sensor
flips have already changed the image and Bayer phase. The cache invalidates
for table, dimensions, sampling pattern and layout changes; black-level
changes do not require rebuilding the gain field.

For the black-subtracted signal, the corrected statistic is
`clamp(black + gain*(raw-black), 0, max)`. Restoring the pedestal matters:
AWB later subtracts black times pixel count, and AGC folds BLC histogram bins.
Multiplying raw DN directly would amplify black twice. Both greens are
corrected before their average, saturation rejection and histogram formation.

Q10 rounding limits gain error to 1/2048. Existing sparse line/pixel sampling
and low-bit precision of packed RAW statistics are retained; these are not
GPU readback statistics or a bit-exact mean of demosaiced output. The current
renderer/stats window and padded-stride conventions are followed rather than
silently corrected in the same patch. Maintainers may prefer to normalize
those conventions in separate changes first.

The map costs about 0.99 MiB at a 1920x1080 statistics window and roughly
518,400 additional integer multiplications per statistics pass. Updates are
cached instead of doing bilinear floating-point interpolation for every
frame/sample. Cortex-A53 timing and power on the rebased upstream series
have not been measured.

## Evidence and remaining image-quality gaps

Each mail commit passed upstream checkstyle and a fresh aarch64/trixie cross
build without compiler warnings. Tests checked all 481 gain codes, 300,000
shader-reference gain comparisons and 840 SwStatsCpu integration cases.
Focused Doxygen warnings-as-errors passed. WSL1's missing memfd_create was
substituted only in the standalone test binary, not production code. QEMU
does not exercise EGL, CAMSS, DMA-buf synchronization or sensor timing.

Reported neutral-plane results after LSC-aware statistics were R/G 0.999,
B/G 0.983, from roughly 5% warmth previously. Default-like AGC targeting
became useful again; the tested room favored 0.12 without a bright-window
upper constraint. Gray-world scene bias, sparse-window coverage and
imperfect per-unit calibration can still affect AWB. The grey-world path
uses the corrected sums on the GPU path; it does not secretly apply another
raw/pre-LSC channel estimate. CPU/fallback statistics intentionally stay raw.

SoftISP has no built-in denoise, sharpening or 50/60 Hz anti-flicker in this
baseline. The included offline cam-snap has guided denoise, sharpening and
50 Hz exposure quantization, but is slower and uses a different AWB/gamma
model. Adding those offline tools does not add their algorithms to libcamera.
Broader illuminants, motion, saturation, mode/transform changes and thermal
behavior remain to be tested before claiming a production-ready camera.
