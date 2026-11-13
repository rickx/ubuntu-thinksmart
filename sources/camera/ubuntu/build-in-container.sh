#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
# Build the thinksmart-camera .deb for the Ubuntu 24.04 (noble) image.
#
# Runs INSIDE an Ubuntu 24.04 arm64 container (emulated is fine, ~20 min):
#
#   docker run --rm --platform linux/arm64 \
#     -v "$PWD":/repo:ro -v "$PWD/out":/out \
#     -e KO=/repo/path/to/s5kc505a.ko ubuntu:24.04 \
#     bash /repo/sources/camera/ubuntu/build-in-container.sh
#
# KO is the s5kc505a.ko built for 6.19.5-msm8953 (see BUILDING.md, camera
# section). The resulting thinksmart-camera_<ver>_arm64.deb lands in /out.
set -euo pipefail

REPO=${REPO:-/repo}
OUT=${OUT:-/out}
KO=${KO:?set KO to the built s5kc505a.ko}
WORK=${WORK:-/src}
LIBCAMERA_COMMIT=be7e83e91348250c015aad6d6f59f1468555bae8
PIPEWIRE_TAG=1.2.6
HERE=$REPO/sources/camera/ubuntu

export DEBIAN_FRONTEND=noninteractive
apt-get update -qq
apt-get install -y -qq --no-install-recommends \
  build-essential git ca-certificates meson ninja-build pkg-config cmake \
  python3-yaml python3-ply python3-jinja2 openssl libssl-dev libyaml-dev \
  libgnutls28-dev libudev-dev libevent-dev libdrm-dev libegl-dev libgles-dev \
  libgbm-dev libepoxy-dev libtiff-dev libgstreamer1.0-dev \
  libgstreamer-plugins-base1.0-dev libdbus-1-dev dpkg-dev file >/dev/null

mkdir -p "$WORK" "$OUT"
cd "$WORK"

# --- libcamera 0.8 + PR #6 patches (S5KC505A helper, LSC-aware GPU stats) ---
[ -d libcamera ] || git clone -q https://git.libcamera.org/libcamera/libcamera.git
git -C libcamera checkout -q "$LIBCAMERA_COMMIT"
git -C libcamera -c user.name=build -c user.email=build@localhost \
  am -q "$REPO"/sources/patches/libcamera/000[12]-*.patch
meson setup "$WORK/build-lc" libcamera --prefix=/opt/libcamera --libdir=lib \
  --buildtype=release -Dpipelines=simple -Dipas=softisp -Dcam=enabled \
  -Dqcam=disabled -Dgstreamer=enabled -Dpycamera=disabled \
  -Dlc-compliance=disabled -Dtest=false -Ddocumentation=disabled \
  -Dtracing=disabled -Dv4l2=enabled -Dsoftisp-gpu=enabled -Dlibdw=disabled \
  -Dlibunwind=disabled -Dcam-output-sdl2=disabled -Dcam-output-kms=enabled \
  -Dcam-jpeg=disabled -Dapps-output-dng=enabled
ninja -C "$WORK/build-lc"
meson install -C "$WORK/build-lc" --quiet
install -m 644 "$REPO/sources/camera/config/s5kc505a.yaml" \
  /opt/libcamera/share/libcamera/ipa/softisp/s5kc505a.yaml

# --- PipeWire 1.2.6 libcamera SPA plugin against libcamera 0.8 ---
[ -d pipewire ] || git clone -q --depth 1 --branch "$PIPEWIRE_TAG" \
  https://gitlab.freedesktop.org/pipewire/pipewire.git
git -C pipewire apply "$HERE/pipewire-1.2.6-libcamera-0.8.patch"
PKG_CONFIG_PATH=/opt/libcamera/lib/pkgconfig meson setup "$WORK/build-pw" pipewire \
  --prefix=/usr --libdir=lib/aarch64-linux-gnu --buildtype=release \
  -Dauto_features=disabled -Dspa-plugins=enabled -Dlibcamera=enabled \
  -Dsession-managers=[] -Dexamples=disabled -Dtests=disabled -Dman=disabled \
  -Ddocs=disabled -Dpipewire-alsa=disabled -Dpipewire-jack=disabled \
  -Dpipewire-v4l2=disabled -Dcpp_std=c++20
ninja -C "$WORK/build-pw" spa/plugins/libcamera/libspa-libcamera.so

# --- assemble the package ---
P=$WORK/pkg
rm -rf "$P"
cp -a "$HERE/pkg" "$P"
mkdir -p "$P/opt" "$P/lib/modules/6.19.5-msm8953/extra" \
  "$P/usr/lib/aarch64-linux-gnu/spa-0.2/libcamera" \
  "$P/usr/lib/aarch64-linux-gnu/gstreamer-1.0" "$P/usr/share/doc/thinksmart-camera"
cp -a /opt/libcamera "$P/opt/"
rm -r "$P/opt/libcamera/include" "$P/opt/libcamera/lib/pkgconfig"
# Do NOT strip ipa_softisp.so: its .sign signature covers the file as built.
strip --strip-unneeded "$P"/opt/libcamera/lib/*.so.0.8.0 "$P/opt/libcamera/bin/cam" \
  "$P/opt/libcamera/libexec/libcamera/softisp_ipa_proxy" \
  "$P/opt/libcamera/libexec/libcamera/v4l2-compat.so" \
  "$P"/opt/libcamera/lib/gstreamer-1.0/*.so
strip --strip-unneeded -o "$P/usr/lib/aarch64-linux-gnu/spa-0.2/libcamera/libspa-libcamera.so" \
  "$WORK/build-pw/spa/plugins/libcamera/libspa-libcamera.so"
chmod 644 "$P/usr/lib/aarch64-linux-gnu/spa-0.2/libcamera/libspa-libcamera.so"
ln -s /opt/libcamera/lib/gstreamer-1.0/libgstlibcamera.so \
  "$P/usr/lib/aarch64-linux-gnu/gstreamer-1.0/libgstlibcamera.so"
install -m 644 "$KO" "$P/lib/modules/6.19.5-msm8953/extra/s5kc505a.ko"
install -m 644 "$REPO/sources/camera/NOTICE.md" "$HERE/pipewire-1.2.6-libcamera-0.8.patch" \
  "$P/usr/share/doc/thinksmart-camera/"
git -C libcamera format-patch -2 --stdout > "$P/usr/share/doc/thinksmart-camera/libcamera-patches.mbox"

VERSION=$(awk '/^Version:/{print $2}' "$P/DEBIAN/control")
dpkg-deb --build --root-owner-group "$P" "$OUT/thinksmart-camera_${VERSION}_arm64.deb"
ls -l "$OUT/thinksmart-camera_${VERSION}_arm64.deb"
