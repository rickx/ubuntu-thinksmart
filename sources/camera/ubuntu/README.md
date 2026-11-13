# Camera on the Ubuntu 24.04 image

SPDX-License-Identifier: CC-BY-4.0

The S5KC505A front camera from PR #6 works on the Ubuntu 24.04 image
(kernel `6.19.5-msm8953`, clang/KCFI) and is baked into the 2026-11-13 image.
Applications see it as a normal PipeWire camera ("Built-in Front Camera").

Verified on the device (2026-11-13):

- `s5kc505a 3-0037: S5KC505A found, id 0x5034/0x5203` at boot, module autoloaded
- libcamera `cam`: 1280x960 XBGR8888 at a steady 30 fps through the GPU SoftISP,
  AGC/AWB working
- GStreamer `libcamerasrc` and PipeWire (`pipewiresrc`, 640x480 default) capture
- after a cold reboot: camera source and speaker both present, no PipeWire crash

## What is installed

Everything except the DTB comes from one package,
[`prebuilt/thinksmart-camera_0.2_arm64.deb`](../../../prebuilt/thinksmart-camera_0.2_arm64.deb):

| Path | Purpose |
| --- | --- |
| `/lib/modules/6.19.5-msm8953/extra/s5kc505a.ko` | sensor driver, clang-18 + KCFI build |
| `/etc/modules-load.d/s5kc505a.conf` | load the driver at boot (it does not autoload from the DT) |
| `/opt/libcamera/` | libcamera v0.8.0 + the two PR #6 patches (`simple` pipeline, `softisp` IPA, GPU SoftISP, `cam`, `libcamerify`, GStreamer element) and `s5kc505a.yaml` tuning |
| `/etc/ld.so.conf.d/thinksmart-libcamera.conf` | makes `/opt/libcamera/lib` visible (sonames `.so.0.8` do not clash with Ubuntu's libcamera) |
| `/usr/lib/aarch64-linux-gnu/spa-0.2/libcamera/libspa-libcamera.so` | PipeWire 1.2.6 libcamera plugin rebuilt against libcamera 0.8 |
| `/usr/lib/aarch64-linux-gnu/gstreamer-1.0/libgstlibcamera.so` | symlink to the `libcamerasrc` element in `/opt/libcamera` |
| `/usr/lib/udev/rules.d/70-thinksmart-camera.rules` | `/dev/dma_heap/*` group `video` |
| `/etc/wireplumber/main.lua.d/51-thinksmart-camera.lua` | hide the raw "Qualcomm Camera Subsystem" and Venus V4L2 nodes |

It depends on `libevent-core-2.1-7t64` and `libevent-pthreads-2.1-7t64` (for `cam`)
and conflicts with `pipewire-libcamera` / `gstreamer1.0-libcamera`, which would
provide the same files built against Ubuntu's older libcamera.

The board DTB is not in the package. It lives on the boot partition:
[`prebuilt/apq8053-lenovo-cd-18781y.dtb`](../../../prebuilt/apq8053-lenovo-cd-18781y.dtb)
is the image's DTB with the PR #6 camera node applied (S5KC505A at CCI0 `0x37`,
MCLK0 24 MHz, reset GPIO40 / powerdown GPIO39 active-low, GPIO119/118 gated
2.8 V / 1.2 V rails, VIO L6, mclk0 pinctrl moved from the CCI node to the
sensor). lk2nd only uses `fdtdir /`, so the file must replace
`/boot/apq8053-lenovo-cd-18781y.dtb`; an extlinux `fdt` line is ignored.

## Things that were needed beyond PR #6

- **DMA-BUF heap access.** The SoftISP allocates from a DMA-BUF heap. This
  kernel has only the CMA heaps (`default_cma_region`, `reserved`), root-only by
  default; without the udev rule a normal user (and PipeWire) gets the RAW
  stream only ("Could not open any dma-buf provider").
- **PipeWire plugin patch** ([pipewire-1.2.6-libcamera-0.8.patch](pipewire-1.2.6-libcamera-0.8.patch)):
  build with C++20, follow two renamed libcamera APIs, and skip array controls
  (ColourGains, Ccm). Unpatched, the plugin asserts in
  `ControlValue::get<float>()` and PipeWire crash-loops, which also kills audio.
- **No `system_heap` module** exists for this kernel; the PR's
  `thinksmart-camera-start` script is not used (module loading is done by
  `modules-load.d`, exposure by libcamera AGC).

## Using it

~~~sh
wpctl status                                   # Video → Sources: Built-in Front Camera
/opt/libcamera/bin/cam -l                      # Internal front camera
/opt/libcamera/bin/cam -c 1 --capture=30 \
  -s role=viewfinder,width=1280,height=960,pixelformat=XBGR8888 --file='f-#.bin'
gst-launch-1.0 libcamerasrc ! video/x-raw,width=1280,height=960 ! videoconvert ! autovideosink
/opt/libcamera/bin/libcamerify <v4l2-only-app>  # for apps that open /dev/video* directly
~~~

PipeWire-based applications (Firefox, Chromium, OBS, GNOME Snapshot, the
camera portal) see the camera directly. `pipewiresrc` needs
`gstreamer1.0-pipewire`, which is not preinstalled.

Limits:

- the sensor is mounted landscape in a portrait device; libcamera reports a
  rotation, but applications that ignore it show the picture turned 90°
- applications that open `/dev/video*` directly need `libcamerify`
- no GUI camera application is preinstalled or has been tested yet

## Rebuilding

1. Build `s5kc505a.ko` for `6.19.5-msm8953` with clang-18 (see
   [BUILDING.md](../../../BUILDING.md#s5kc505a-camera-module)).
2. Build the package in an Ubuntu 24.04 arm64 container with
   [build-in-container.sh](build-in-container.sh) (emulated arm64 is fine;
   libcamera takes about 15 minutes):

   ~~~sh
   docker run --rm --platform linux/arm64 \
     -v "$PWD":/repo:ro -v "$PWD/out":/out \
     -e KO=/repo/path/to/s5kc505a.ko ubuntu:24.04 \
     bash /repo/sources/camera/ubuntu/build-in-container.sh
   ~~~

   It builds libcamera at `be7e83e9` + `sources/patches/libcamera/000[12]`,
   PipeWire 1.2.6's libcamera plugin with the patch above, and assembles
   [pkg/](pkg/) into `thinksmart-camera_<version>_arm64.deb`.
3. Install with `sudo dpkg -i thinksmart-camera_*_arm64.deb` and reboot (or
   restart `pipewire wireplumber` as the user).

The IPA module `ipa_softisp.so` is signed at build time; the script does not
strip it, and the module, its `.sign` file and `libcamera.so` must come from
the same build.
