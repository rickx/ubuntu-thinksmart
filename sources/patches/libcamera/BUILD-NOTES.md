# Libcamera GPU SoftISP build

Both libcamera patches are carried in this PR under
`sources/patches/libcamera/`. They have not been submitted separately to the
libcamera-devel mailing list. Maintainers or any contributor are welcome
to submit the series upstream on the author's behalf.

SPDX-License-Identifier: CC-BY-4.0

The two mail patches (plus cover letter) target upstream master
`be7e83e91348250c015aad6d6f59f1468555bae8`, fetched 2026-11-07. Apply only
`0001-*.patch` and `0002-*.patch` with git am; `0000` is the cover letter.
The resulting commits in the prepared series are
`b588ec07aa160b02e74afe38faa5a2726cfadef6` (sensor helper) and
`4687007e00849b555ac23934ddefb3445f8f2c2d` (LSC-aware GPU statistics).
The cover discloses AI assistance in ordinary prose; commits preserve Mei
Tao's authorship and sign-off. Patch code retains LGPL-2.1-or-later notices,
overriding the surrounding repository's GPL patch-directory default.

The runtime tested on the derivative image was tag v0.8.0,
`df8e348d64b4932a0676256ed24f0da3ee9438bc`, with equivalent changes. This
mail series was rebased, independently compiled at each commit and tested
under QEMU. It has not been run on upstream Ubuntu hardware.

## Dependencies and cross file

The recorded host is Debian trixie amd64 inside WSL, using official Debian
arm64 multiarch libraries, GCC 14.2.0-19, glibc 2.41, Meson 1.7.0 and Ninja
1.12.1. Do not use Ubuntu host libraries to link a trixie deliverable. Enable
arm64 multiarch and install matching trixie build dependencies for yaml,
gnutls, udev, event, tiff, DRM, EGL, GLES and GBM, plus the aarch64 GCC/G++
cross toolchain, pkgconf, Python build modules and qemu-user. Read the pinned
source's dependency list for exact package requirements.

`aarch64-trixie.ini` is the validated configuration, copied unchanged.
v0.8 needs Meson >=1.0.1, C++20 and GCC >=10 / Clang >=9; its generated Ninja
file requires >=1.8.2. Trixie satisfies these requirements. The IPA is named
`softisp` in 0.8; the pipeline remains `simple`.

~~~sh
git clone https://git.libcamera.org/libcamera/libcamera.git libcamera
cd libcamera
git checkout be7e83e91348250c015aad6d6f59f1468555bae8
git am /path/to/ubuntu-thinksmart/sources/patches/libcamera/000[12]-*.patch
meson setup ../build-camera \
  --cross-file /path/to/ubuntu-thinksmart/sources/patches/libcamera/aarch64-trixie.ini \
  --prefix=/srv/thinksmart/data/opt/libcamera --libdir=lib --buildtype=release \
  -Dpipelines=simple -Dipas=softisp -Dcam=enabled -Dqcam=disabled \
  -Dgstreamer=disabled -Dpycamera=disabled -Dlc-compliance=disabled \
  -Dtest=false -Ddocumentation=disabled -Dtracing=disabled -Dv4l2=enabled \
  -Dsoftisp-gpu=enabled -Dlibdw=disabled -Dlibunwind=disabled \
  -Dcam-output-sdl2=disabled -Dcam-output-kms=enabled -Dcam-jpeg=disabled \
  -Dapps-output-dng=enabled
ninja -C ../build-camera -j4
DESTDIR="$PWD/../stage-camera" meson install -C ../build-camera
~~~

Stage root is `srv/thinksmart/data/opt/libcamera/`. Install the camera wrapper
and tuning from `sources/camera/` afterwards. Set `LIBCAMERA_SOFTISP_MODE=gpu`;
Mesa must support the surfaceless EGL path, DMA buffers and GPU drivers.
Cross-build completion or QEMU cam --version does not test those interfaces.

Libcamera signs its IPA module with a build-local private key. Keep the
installed module and `.sign` files together with the matching compiled-in
public key/library. Use the same build's `ipa_verify` to validate the module.
Do not distribute the private key, mix an IPA with another build's library,
or disable signature checks to mask a mismatch. If adding bundled runtime
libraries, separately audit every ELF NEEDED entry and include the relevant
licenses/source-offer obligations in any binary release asset.

## Recorded verification and source records

Each commit passed unmodified upstream checkstyle with no issues and a fresh
cross build with no compiler warnings. The helper test checks all 481 analog
gain codes. LSC tests compare 300,000 gains to a shader reference and exercise
840 actual SwStatsCpu integration cases; Q10 error <=1/2048. Focused Doxygen
warnings-as-errors and documentation assertions passed. Commit messages were
later reworded without changing either source tree; checkstyle and exact
patch-application/tree checks were rerun successfully.

The original build records are `libcamera-build/BUILD-NOTES.md`,
`reproduce-build.sh` and `aarch64-trixie.ini` in the contributor's delivery
folder. The script contains absolute WSL paths; the commands above are its
portable equivalent with the two patches applied. The original binary
package additionally passed IPA signature, AArch64 NEEDED/loader closure and
QEMU cam version checks. Those checks are not hardware camera tests.

No libcamera binary tarball or generated private signing key is committed.
Binary packages, full logs and standalone test programs may be attached as
release assets. For mailing-list submission, subscribe at
https://lists.libcamera.org and use
`git send-email --to libcamera-devel@lists.libcamera.org *.patch` from a
directory containing only this mail series, after reviewing the cover letter.
