# S5KC505A camera contribution (PR #6)

SPDX-License-Identifier: CC-BY-4.0

Description of pull request #6 by Mei Tao (meotao), merged 2026-11-13, kept
as the record of the contribution's evidence, validation limits and data
provenance.

> **Update 2026-11-13:** the "not validated on this repository's Ubuntu 24.04
> image" statement below no longer applies. The driver was rebuilt with
> clang-18/KCFI and the full stack (libcamera 0.8 GPU SoftISP, PipeWire) runs
> on the Ubuntu image; see [ubuntu/README.md](ubuntu/README.md).

## Add S5KC505A camera driver, GPU SoftISP integration and calibration tools

This PR carries both libcamera patches in `sources/patches/libcamera/`.
They have not been submitted separately to the libcamera-devel mailing
list. The maintainers or any contributor are welcome to submit that series
upstream on the author's behalf.

The front camera currently has a research plan but no contributed driver or
complete Linux userspace path. This series adds the external S5KC505A V4L2
driver, a symbolic board DTS patch, libcamera sensor/LSC-statistics fixes,
generic reference tuning, optional offline capture/calibration tools and
contributor documentation. It keeps camera integration separate from the
existing audio module build.

### Result and scope

- Driver: two-lane RAW10, default binned 1304x980, optional 2608x1960,
  exposure/gain/timing controls and Bayer-aware horizontal/vertical flips.
  `flip_ctrls=0` supports the older libcamera 0.4 pipeline.
- Board patch: actual CCI bus 0 labels, 24 MHz MCLK0, active-low GPIO40/39,
  fixed 2.8 V/1.2 V rails from GPIO119/118, VIO from L6 and the CSIPHY0 graph.
- Libcamera: mail-ready helper and GPU LSC-aware statistics series on master
  `be7e83e91348250c015aad6d6f59f1468555bae8`, build notes and trixie cross file.
  GPU AE/AWB follow the corrected signal; CPU/no-LSC statistics stay raw.
- Userspace: generic reference tuning, cam-snap, per-unit cam-flat, a tuning
  converter, a separate-prefix wrapper and a module-loading oneshot.
- Documentation: driver/power/Bayer details, reference-data decoding and
  flat-field workflow, ISP design/limits and concise deployment status.

No binary camera/libcamera artifact, single-unit residual, OTP dump, captured
image or private signing key is added. Binary builds can be attached later
as release assets with dependency/license records.

### Platform evidence and validation limits

Reported hardware results are from the derivative **ThinkSmart Clean
2026.11.01**, Debian trixie, GCC/no-KCFI kernel `6.19.5-thinksmart+`,
Qualcomm MSM8953 / qcom-camss, S5KC505A and Mesa freedreno FD506. Binned GPU
SoftISP reached about 30 fps. LSC-aware statistics improved a neutral-plane
measurement from about 5% warm to R/G 0.999, B/G 0.983; target 0.12 was a
useful exposure setting in the tested daylight-window scene.

**This has not been validated on this repository's Ubuntu 24.04 image.** Its
pmOS msm8953 kernel uses clang-18 and KCFI; the external module must be rebuilt
with its exact prepared kernel and matching clang/LLVM toolchain. A GCC-built
module can appear loaded but fail to reach probe. The integrated C body and
register tables match the earlier built driver, aside from notices and a
trailing blank line; no new module build or hardware test was performed for
this packaging task. Generic committed tuning omits the unit residual used
in the reported image measurements.

In WSL, cpp/dtc successfully compiled the patched board with full kernel
includes, and DTB assertions verified clocks, GPIO polarities, supplies and
reciprocal endpoints. There is one additional single-port graph advisory
plus existing SoC warnings; no dt-schema validation or boot test is claimed.
Python AST and shell syntax checks passed without executing repository or
archive programs. Tuning/schema/provenance and exact libcamera mail-copy
checks passed. Fresh application/tree checks verify the exported outer patch
series. No device access, remote push or email submission occurred.

The embedded libcamera series separately passed checkstyle and fresh cross
builds at both commits, all 481 helper gain codes, 300,000 shader-reference
gain comparisons, 840 SwStatsCpu integration cases and focused Doxygen
checks. Its later message-only revision preserved code trees and reran
checkstyle/application validation. These QEMU checks do not exercise EGL,
DMA-buf, CAMSS or sensor streaming.

### Provenance and maintainer decisions

The register sequences in `s5kc505a_regs.h` and LSC/CCM/gamma numbers in
`s5kc505a_tuning.json` / `s5kc505a.yaml` were decoded from **proprietary Lenovo
firmware libraries/chromatix**, including `libmmcamera_s5kc505a.so` and the
0x0309 chromatix family. They are numeric source/data rather than the blobs
themselves. No Lenovo proprietary binary is added. This contribution does
not assert that Lenovo licensed its original libraries under GPL or MIT;
maintainers must decide whether to accept, separately host or replace the
decoded tables. See `sources/camera/NOTICE.md` for the detailed mapping.

Driver implementation carries GPL-2.0-only, userspace scripts MIT and new
documentation CC-BY-4.0. Libcamera patches retain LGPL-2.1-or-later; the board
DTS retains BSD-3-Clause. File-level notices override directory defaults.

Remaining work includes a sensor DT binding, Ubuntu/KCFI build and boot
validation, broad scene/power-management testing, review of recovered numeric
data provenance and a supported per-unit calibration method. The generic
Samsung OTP/SRAM experiment did not correct shading and is explicitly opt-in;
no proven S5KC505A OTP decoder is claimed. GPU SoftISP still lacks real-time
denoise, sharpening and anti-flicker; the offline tool's features are separate.

The implementation and contribution preparation used an AI coding assistant.
