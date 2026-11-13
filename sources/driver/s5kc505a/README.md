# S5KC505A external sensor module

SPDX-License-Identifier: CC-BY-4.0

This directory contains an external V4L2 subdevice driver for the front camera
of the Lenovo ThinkSmart View CD-18781Y. It is separate from the audio module
Makefile in the parent directory. See [camera integration](../../camera/README.md)
for the device tree, build and userspace instructions.

The C source, register table and Kbuild Makefile carry GPL-2.0-only notices.
The register sequences were decoded from Lenovo's proprietary
libmmcamera_s5kc505a.so; this is numeric source data, not an included binary.
Maintainers need to review that provenance before redistribution. This notice
does not grant a license to Lenovo's original libraries.

The driver has three read-only module parameters: bayer_order (default 1,
GRBG), full_res (default false), and flip_ctrls (default true). Disable
flip_ctrls for libcamera 0.4 compatibility; this leaves orientation correction
to the application. Use libcamera 0.8 with both flip controls for the normal
180-degree mounting correction.
