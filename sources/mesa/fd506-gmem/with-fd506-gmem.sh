#!/bin/sh
# SPDX-License-Identifier: MIT
# Apply the private Mesa library only to the requested application and its children.
set -eu
if [ "$#" -eq 0 ]; then
    echo "Usage: $0 APPLICATION [ARGUMENT ...]" >&2
    exit 2
fi
VERSION=$(dpkg-query -W -f='${Version}' mesa-libgallium)
if [ "$VERSION" != 25.2.8-0ubuntu0.24.04.2 ]; then
    echo "Unsupported Mesa runtime: $VERSION; rebuild/revalidate before using this override." >&2
    exit 1
fi
LIBDIR=/opt/thinksmart-gpu/lib/aarch64-linux-gnu
if [ ! -r "$LIBDIR/libgallium-$VERSION.so" ] || [ ! -r "$LIBDIR/dri/msm_dri.so" ]; then
    echo "Install the matching thinksmart-gpu.tar.gz into /opt/thinksmart-gpu first." >&2
    exit 1
fi
export LIBGL_DRIVERS_PATH="$LIBDIR/dri"
export LD_LIBRARY_PATH="$LIBDIR${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
exec "$@"
