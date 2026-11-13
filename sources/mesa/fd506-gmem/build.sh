#!/bin/sh
# SPDX-License-Identifier: MIT
set -eu

HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
if [ "$#" -ne 3 ]; then
    echo "Usage: $0 UBUNTU_MESA_SOURCE BUILD_DIR STAGE_DIR" >&2
    exit 2
fi
[ "$(dpkg-architecture -qDEB_HOST_ARCH)" = arm64 ] || {
    echo "Build on an ARM64 Ubuntu 24.04 host." >&2; exit 1;
}
SOURCE=$(CDPATH= cd -- "$1" && pwd)
mkdir -p "$2" "$3"
BUILD=$(CDPATH= cd -- "$2" && pwd)
STAGE=$(CDPATH= cd -- "$3" && pwd)
cd "$SOURCE"
VERSION=$(dpkg-parsechangelog -S Version)
[ "$VERSION" = 25.2.8-0ubuntu0.24.04.2 ] || {
    echo "Expected Ubuntu Mesa 25.2.8-0ubuntu0.24.04.2, got $VERSION" >&2; exit 1;
}

for patch_file in "$HERE"/000*.patch; do
    if patch --batch --forward --dry-run -p1 < "$patch_file" >/dev/null 2>&1; then
        patch --batch --forward -p1 < "$patch_file"
    else
        # Permit rebuilding an already-patched source tree; reject other failures.
        patch --batch --dry-run -R -p1 < "$patch_file" >/dev/null
    fi
done
python3 "$HERE/test-gmem.py" "$SOURCE"

if [ -f "$BUILD/meson-private/coredata.dat" ]; then
    set -- --reconfigure
else
    set --
fi
meson setup "$@" "$BUILD" --prefix=/opt/thinksmart-gpu --libdir=lib/aarch64-linux-gnu \
    -Dbuildtype=release -Dgallium-drivers=freedreno -Dvulkan-drivers= \
    -Dllvm=disabled -Dplatforms=x11,wayland -Dglx=dri -Degl=enabled -Dgbm=enabled \
    -Dvideo-codecs= -Dgallium-va=disabled -Dgallium-vdpau=disabled \
    -Dgles1=disabled -Dbuild-tests=false -Dtools=
ninja -C "$BUILD" -j2
DESTDIR="$STAGE" meson install -C "$BUILD" --no-rebuild

# Bundle only Gallium. The target retains its matching system EGL/GBM and DRI shim.
BUNDLE=$(mktemp -d "$STAGE/.runtime.XXXXXX")
trap 'rm -rf "$BUNDLE"' EXIT HUP INT TERM
LIBDIR="$BUNDLE/lib/aarch64-linux-gnu"
mkdir -p "$LIBDIR/dri"
cp "$STAGE/opt/thinksmart-gpu/lib/aarch64-linux-gnu/libgallium-$VERSION.so" "$LIBDIR/"
ln -s /usr/lib/aarch64-linux-gnu/dri/libdril_dri.so "$LIBDIR/dri/msm_dri.so"
tar -czf "$STAGE/thinksmart-gpu.tar.gz" -C "$BUNDLE" .
sha256sum "$STAGE/thinksmart-gpu.tar.gz"
