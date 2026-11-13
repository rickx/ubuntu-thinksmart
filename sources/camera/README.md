# ThinkSmart View S5KC505A camera

SPDX-License-Identifier: CC-BY-4.0

This contribution integrates a front-camera driver, a board DTS patch,
libcamera GPU SoftISP fixes and optional offline snapshot/calibration tools.
Reported streaming and image-quality results are from the derivative
**ThinkSmart Clean 2026.11.01**, Debian trixie, kernel
`6.19.5-thinksmart+` (GCC, no KCFI), Qualcomm MSM8953 / qcom-camss and Adreno
506.

**Ubuntu 24.04 (2026-11-13):** merged as PR #6 and verified on the Ubuntu
image (clang-18/KCFI module build, 30 fps GPU SoftISP, PipeWire camera). It is
baked into the 2026-11-13 image as the `thinksmart-camera` package plus the
patched DTB; see [ubuntu/README.md](ubuntu/README.md) for what is installed,
what was needed beyond this contribution and how to rebuild it. The original
pull request description (evidence, validation limits, provenance) is kept in
[CONTRIBUTION.md](CONTRIBUTION.md).

The register sequences and LSC/CCM/gamma tuning numbers were decoded from
proprietary Lenovo firmware libraries, not obtained as freely licensed vendor
source. No proprietary blob is included. Please read [NOTICE.md](NOTICE.md)
before accepting or redistributing the data; acceptance is a maintainer
decision. Scripts and implementation code have explicit license notices.

## Layout

| Location | Purpose |
| --- | --- |
| `../driver/s5kc505a/` | External RAW10 V4L2 driver, tables and Kbuild file |
| `../patches/0001-arm64-dts-qcom-apq8053-lenovo-cd-18781y-add-S5KC505A-front-camera.patch` | Symbolic camera supplies, GPIOs and graph |
| `../patches/libcamera/` | Two-commit libcamera mail series and build notes |
| `config/s5kc505a.yaml` | Generic vendor-derived SoftISP tuning; no unit residual |
| `tools/` | cam-snap, cam-flat, tuning converter and opt-in OTP experiments |
| `scripts/` and `systemd/` | Libcamera wrapper and boot-time module loading (Debian derivative layout) |
| `ubuntu/` | Ubuntu 24.04 package: build script, package files, PipeWire plugin patch |

## Build the external module

Use a prepared build tree for the **exact target kernel and configuration**,
including generated headers and `Module.symvers`. The upstream pmOS msm8953
kernel uses clang-18 and KCFI: compile its external modules with clang-18 and
the matching LLVM tools. A GCC module may appear loaded yet never probe on
that KCFI kernel. The earlier GCC/no-KCFI derivative module is not a portable
binary for the Ubuntu image. See [BUILDING.md](../../BUILDING.md).

~~~sh
export KSRC=/path/to/matching/prepared/kernel
export CAMERA_BUILD="$HOME/s5kc505a-src"
mkdir -p "$CAMERA_BUILD"
cp sources/driver/s5kc505a/{s5kc505a.c,s5kc505a_regs.h,Makefile} "$CAMERA_BUILD/"
make ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- LLVM=-18 CC=clang-18 \
     -C "$KSRC" M="$CAMERA_BUILD" modules
modinfo "$CAMERA_BUILD/s5kc505a.ko"
~~~

Check vermagic, symbol versions and compiler/KCFI settings against the target.
The driver uses recent V4L2 CCI and subdevice-state APIs; it is not intended
for Lenovo's Android 3.18 kernel. Install into the target's updates directory
and run `depmod` using the image's normal deployment method.

## Device tree and power

Apply the camera DTS patch to the pmOS msm8953 source after the existing board
support patches, build its DTB and deploy using the project's normal boot
image process. Do not replace an entire DTB with the original decompilation.

~~~sh
git -C "$KSRC" apply /path/to/ubuntu-thinksmart/sources/patches/0001-arm64-dts-qcom-apq8053-lenovo-cd-18781y-add-S5KC505A-front-camera.patch
~~~

The supplied kernel labels CCI bus 0 `cci_i2c0` rather than `cci0_i2c0`.
The patch uses that actual label, `cci`, `camss` and existing pinctrl labels.
It uses MCLK0 at 24 MHz, reset GPIO40 low-active, powerdown GPIO39 low-active,
VIO from L6, VANA from L17 through GPIO119 (2.8 V) and VDIG from L2 through
GPIO118 (1.2 V). The 480 MHz two-lane endpoint maps to CAMSS CSIPHY0. Review
those labels if applying to another kernel revision.

The source was preprocessed and compiled with cpp/dtc in WSL. Dtc reports one
additional single-port `graph_child_address` advisory, plus existing SoC
warnings. This was a syntax/graph-property check, not dt-schema validation or
a new boot test. A dedicated samsung,s5kc505a binding remains future work.

## Libcamera and tuning

Build libcamera 0.8 with both patches as described in
[BUILD-NOTES.md](../patches/libcamera/BUILD-NOTES.md). The mail series targets
master `be7e83e91348250c015aad6d6f59f1468555bae8`; the earlier tested runtime
was v0.8.0 with the equivalent changes. Keep it separate from system libcamera.
Use the GPU SoftISP; the CPU renderer does not apply lens shading.

~~~sh
P=/srv/thinksmart/data/opt/libcamera
install -m 755 sources/camera/scripts/lcam "$P/lcam"
install -m 644 sources/camera/config/s5kc505a.yaml "$P/share/libcamera/ipa/softisp/s5kc505a.yaml"
LIBCAMERA_SOFTISP_MODE=gpu "$P/lcam" cam -l
LIBCAMERA_SOFTISP_MODE=gpu "$P/lcam" cam -c 1 --capture=10 --file='frame-#.ppm'
~~~

The helper enables tunable mean-luminance AGC and EV compensation. The tuning
starts at `relativeLuminanceTarget: 0.12` without a highlight constraint; tune
for your scene. GPU statistics now follow LSC before AWB/CCM. The LSC grids
are rotated 180 degrees for the driver's default flip controls and board
rotation. If using `flip_ctrls=0` or another transform, regenerate with the
appropriate orientation (converter `--no-rot180` for unflipped input).

## Module/service ordering

Install `scripts/thinksmart-camera-start` as
`/usr/local/sbin/thinksmart-camera-start` and the unit into
`/etc/systemd/system/thinksmart-camera.service`, then reload systemd and enable
the unit through normal image deployment. The oneshot loads DMA heaps, V4L2,
CCI, CAMSS and the sensor, then sets initial manual exposure/gain for direct
V4L2 captures. Libcamera's AGC replaces those manual values during streaming.
It is not a camera server; stop competing capture applications before use.
Built-in drivers do not need separate module files.

Libcamera 0.4 has a simple-pipeline Bayer propagation bug with the flip
controls and this 180-degree mounting: link validation fails with
`Broken pipe`. Use the patched 0.8 stack. For 0.4, set
`options s5kc505a flip_ctrls=0` in a modprobe configuration before loading the
driver, and rotate the captured image in software. Default `full_res=0` offers
1304x980; `full_res=1` additionally exposes 2608x1960.

## Offline snapshot and per-unit flat-field

Install `python3-numpy`, `v4l-utils` (including media-ctl) and `ffmpeg`. Copy
`tools/` to `/srv/thinksmart/data/tools/cam-isp/`. Wrappers use system Python;
`CAM_ISP_PYTHON=/path/to/venv/bin/python` selects another interpreter. They
raise only their own OOM score because the board has about 2 GB RAM.

~~~sh
cd /srv/thinksmart/data/tools/cam-isp
./cam-snap shot.jpg
./cam-snap --full --denoise 1.0 --sharp 0.6 --raw shot.raw shot-full.jpg
./cam-flat --target 300
./cam-flat --target 300 --save
python3 make_libcamera_tuning.py
~~~

These tools own/reconfigure the media graph and need camera/I2C/module
permissions (the original workflow used root). Do not run alongside libcamera.
They force unflipped GRBG and rotate the final image in software. The offline
50 Hz exposure quantization, denoise and sharpening are separate from SoftISP.
`--full` can reload the module and is slow; do not use while another client
has the device open.

No residual from the contributor's unit is shipped. Use an evenly illuminated
diffuser filling the entire field, avoid clipping, inspect multiple frames
and generate your own `residual.json` with cam-flat. It corrects chroma fully
and luminance partially by default; `CAM_FLAT_LUMA=1` requests full flattening
at the expense of corner noise. The converter emits Ccm/Lsc sections only:
replace those two sections in `config/s5kc505a.yaml`, retaining BlackLevel,
Awb, Adjust and Agc. Delete residual.json and use `--no-residual` to return
to generic tuning. Do not commit unit calibration or OTP dumps.

The `tools/otp/` scripts are opt-in research helpers, not an installation
step. They can write volatile sensor registers. No supported OTP-to-LSC
decoder exists, and the SRAM experiment did not produce useful correction.
See the [driver](../../research/camera/analysis/s5kc505a-driver-notes.md),
[tuning](../../research/camera/analysis/isp-tuning-notes.md) and
[SoftISP](../../research/camera/analysis/libcamera-softisp-notes.md) analyses.
