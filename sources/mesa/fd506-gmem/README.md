# FD506 depth/stencil MSAA fix

Qt WebEngine can load a page successfully while showing an empty viewport on the
ThinkSmart View. In the tested Mesa version, Freedreno sizes multisampled
depth/stencil buffers twice: `fdl5_layout` already includes the resource sample
count in `layout.cpp`, then `gmem_key_init` multiplies it by framebuffer samples
again. A 4x MSAA batch can exceed the FD506's GMEM even at the minimum tile size,
leaving `calc_nbins` in an unbounded search.

The first patch derives bytes per sample from the resource's actual format and
multiplies by framebuffer samples once, including separate stencil. GPU
acceleration, MSAA and GMEM rendering stay enabled. The second patch matches the
ELF symbol-version namespace expected by the existing Ubuntu EGL/GBM libraries.
A filename alias alone does not satisfy those versioned symbol references.

This is an **opt-in post-install fix** tested with Ubuntu Mesa
`25.2.8-0ubuntu0.24.04.2` on ARM64. It has not been integrated into the downloadable
image. The build produces a private Gallium library used only by applications
started through the wrapper; it does not install a global graphics override.

## Source and build

Use an ARM64 Ubuntu 24.04 build host. Obtain the matching Ubuntu source package
from the [Ubuntu ports archive](https://ports.ubuntu.com/pool/main/m/mesa/):

- `mesa_25.2.8-0ubuntu0.24.04.2.dsc`
- `mesa_25.2.8.orig.tar.xz`
- `mesa_25.2.8.orig.tar.xz.asc`
- `mesa_25.2.8-0ubuntu0.24.04.2.debian.tar.xz`

Check the source files against the SHA256 values in the `.dsc` before extracting
with `dpkg-source -x`. Keep Ubuntu's downstream security patches applied; using
only the upstream tarball omits them. The build script checks the source-package
version and refuses other versions.

Build dependencies:

```sh
sudo apt-get install build-essential dpkg-dev patch ninja-build pkg-config \
  libdrm-dev libx11-dev libxext-dev libxfixes-dev libxrandr-dev \
  libxcb-glx0-dev libxcb-shm0-dev libx11-xcb-dev libxcb-dri2-0-dev \
  libxcb-dri3-dev libxcb-present-dev libxcb-randr0-dev libxshmfence-dev \
  libxxf86vm-dev libglvnd-dev libwayland-dev libwayland-egl-backend-dev \
  wayland-protocols libexpat1-dev libelf-dev libzstd-dev \
  python3-venv python3-mako python3-yaml python3-ply python3-packaging bison flex
python3 -m venv --system-site-packages mesa-tools
mesa-tools/bin/pip install meson==1.8.3
export PATH="$PWD/mesa-tools/bin:$PATH"
dpkg-source -x mesa_25.2.8-0ubuntu0.24.04.2.dsc mesa-source
```

Run the build script from this directory, giving it the extracted source and
separate build/staging directories:

```sh
./build.sh /absolute/path/mesa-source /absolute/path/mesa-build /absolute/path/mesa-stage
```

Both X11 and Wayland loader interfaces are built because the existing Ubuntu EGL
library references them, even when the application runs on Wayland. Only the
patched Gallium library and a symlink to the existing DRI shim enter the runtime
bundle. The script prints the SHA256 of `mesa-stage/thinksmart-gpu.tar.gz`.

## Install and use on the device

First confirm the target runtime matches:

```sh
dpkg-query -W -f='${Version}\n' mesa-libgallium
```

It must report `25.2.8-0ubuntu0.24.04.2`. Copy the runtime archive and
`with-fd506-gmem.sh` to the device. The bundle expects the existing
`/usr/lib/aarch64-linux-gnu/dri/libdril_dri.so` shim from that runtime.

```sh
sudo mkdir -p /opt/thinksmart-gpu
sudo tar -xzf thinksmart-gpu.tar.gz -C /opt/thinksmart-gpu
sudo install -m755 with-fd506-gmem.sh /opt/thinksmart-gpu/with-fd506-gmem
/opt/thinksmart-gpu/with-fd506-gmem /path/to/browser [arguments...]
```

The wrapper sets `LIBGL_DRIVERS_PATH` and `LD_LIBRARY_PATH` only for the requested
application and its children. It refuses a different installed Mesa version or a
missing private driver. After a system Mesa upgrade, rebuild and revalidate before
using it again. Add the wrapper to an application launcher only after testing;
back up that launcher first. Launching the original command without the wrapper
is the rollback. Removing the private directory also requires reverting any
launcher that refers to it.

Confirm the application really loaded the private library:

```sh
grep libgallium /proc/APP_PID/maps
```

The mapping should point into `/opt/thinksmart-gpu`. In Qt WebEngine,
`qt.webenginecontext` and `qt.webengine.compositor` logging should identify
`FD506` and `GPU Compositing: Enabled`. Check a real display capture as well as
successful HTTP loading; HTTP 200 alone did not catch this failure.

## Tests and benchmark

```sh
python3 test-gmem.py /absolute/path/mesa-source
```

The unmodified source fails the MSAA assertion. After the first patch it passes
1x/2x/4x cases, packed and separate stencil, capacity and non-overlap checks. The
harness compiles the production FDL cpp expressions and GMEM layout function with
small fixtures; it does not exercise the entire Gallium API or replace device QA.

The optional benchmark needs `qml6`, Qt Quick Controls and Qt WebEngine, plus an
active Wayland session. Run as that session's user from a graphical terminal:

```sh
python3 benchmark-device.py --sysmem
python3 benchmark-device.py
python3 benchmark-device.py --video
```

Over SSH, add `--session-pid PID` for a same-user graphical process; only display
environment variables are copied. Each regular run warms up for five seconds,
then measures a fixed 200-element scroll and animated canvas for 30 seconds.
The video variant checks a canvas capture stream and reports H.264 capability;
it does not test authenticated or DRM-protected playback.

Recorded results are in [performance.json](performance.json), with the device
findings and limitations in the
[validation report](../../../research/video/analysis/fd506-gmem-msaa-fix-2026-10-30.md).

The Mesa patches retain their upstream MIT license. Helper scripts use MIT;
repository documentation follows the root license mapping.
