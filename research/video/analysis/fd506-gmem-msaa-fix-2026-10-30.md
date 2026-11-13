# FD506 GMEM sizing stall with multisampled depth/stencil

## Environment and symptom

Tested on Lenovo ThinkSmart View CD-18781Y, FD506/Adreno 506, ARM64 Ubuntu 24.04,
kernel `6.19.5-msm8953`, Ubuntu Mesa `25.2.8-0ubuntu0.24.04.2`, Qt WebEngine
6.11.1, and a Cage Wayland session. That desktop/browser setup is a device-side
test environment, not a new desktop shipped by this change.

Opening `https://www.bilibili.com/` produced an empty viewport despite successful
DNS, TLS and HTTP 200 responses. A Chromium GPU thread consumed one CPU core.
Disabling GPU rendering or forcing Freedreno sysmem avoided the failure. Those
were diagnostic controls, not the final application configuration.

## Captured cause

GDB located the busy thread in `layout_gmem`, called through
`lookup_gmem_state` and `fd_gmem_render_tiles`. Debug symbols matching the installed
library's build ID were used to resolve the stripped Mesa stack. The kernel
reported GMEM size `0x22000` (139264 bytes) and no GPU faults.

The failing framebuffer was 128x32 with 4x MSAA. Its GMEM key had color cpp 4 and
depth/stencil cpp 64. The minimum tile was 64x32, with 16 KiB buffer alignment:

| Calculation | Required bytes | Fits 139264-byte GMEM? |
|---|---:|---|
| Original, minimum 64x32 tile | 147456 | No |
| Corrected, full 128x32 tile | 81920 | Yes |
| Corrected, 64x32 tile | 49152 | Yes |

In Mesa 25.2.8, `fdl5_layout` initializes `layout.cpp` as format bytes times resource
samples. `gmem_key_init` multiplies that value by framebuffer samples again for
depth/stencil, while color sizing starts from format bytes. The second sample
factor changes the depth/stencil value from 16 to 64 and makes the layout search
unsatisfiable. Increasing the bin count cannot shrink an aligned tile below its
minimum dimensions, so the search does not terminate.

The fix starts from the resource layout's actual format for both depth/stencil
planes, then multiplies by framebuffer samples once. It preserves the existing
GMEM allocator, alignment, MSAA settings and hardware compositing path.

## Device verification

- The regression harness fails on the original source and passes after the patch
  for 1x/2x/4x MSAA, packed and separate stencil, capacity and non-overlap cases.
  It compiles production expressions/layout code with small fixtures, not a full
  Gallium context or GL conformance suite.
- The ARM64 private library was built from the matching Ubuntu source package
  with its downstream security patches retained. The installed library SHA256 was
  `e16b2011aeb7d0be79e8379bfe129d85c163a360499e83dca76a3307d3cf2934`.
  The build is reproducible from source; identical binary hashes are not promised.
- The PR build script also completed a clean ARM64 build and a repeat build in
  the same directory. Its fresh bundle resolved the device's system EGL symbols
  and exposed the FD506 hardware renderer in an isolated runtime directory.
- Process mappings confirmed the private Gallium library. Qt logging reported
  `FD506`, GPU compositing enabled, native OpenGL output and DMA-BUF support.
- Bilibili painted successfully in normal mode and in a separate test forced
  through GMEM with `FD_MESA_DEBUG=gmem`. The persistent launcher has no forced
  sysmem or disabled-GPU setting.
- The real kiosk address field accepted touch focus, URL typing and Return.
  Refresh and Back worked. Chinese input produced 62 candidates for `nihao`,
  with `你好` first and successfully committed.
- A synthetic canvas video stream advanced its playback time and displayed
  frames. H.264 `canPlayType` reported `probably`; this does not establish
  account-gated Bilibili playback or hardware video decoding.
- After a normal device reboot, the browser autostarted with the private library
  and real address-bar navigation to Bilibili still displayed content.

## Performance

The [benchmark](../../../sources/mesa/fd506-gmem/benchmark-device.py) uses a fixed
200-element scroll plus animated canvas, with a five-second warmup and a
30-second measurement. Three sysmem runs were followed by three patched runs on
one device with `QT_SCALE_FACTOR=1.25`; run order was not randomized. These are controlled-workload results,
not Bilibili frame rates or general performance guarantees.

| Median across three runs | GPU sysmem control | Patched driver |
|---|---:|---:|
| FPS | 52.73 | 59.80 |
| P95 frame interval | 33.3 ms | 16.7 ms |
| CPU, percent of one core | 95.3% | 98.7% |
| Peak PSS per run | 312.8 MiB | 340.1 MiB |

Raw measurements are in
[performance.json](../../../sources/mesa/fd506-gmem/performance.json).

## Limits and deployment

The private library must match the existing Ubuntu EGL/GBM ELF symbol-version
namespace. Replacing only the filename with a symlink failed that check. The
build also needs both X11 and Wayland loader exports to satisfy the existing
EGL library, even though the tested application uses Wayland.

This fix is opt-in and pinned to the tested Ubuntu runtime. Other Mesa versions,
other GPUs, and the public downloadable image have not been validated. No
system-wide driver replacement or image integration was performed.

Graceful Qml Runtime/WebEngine teardown sometimes stalled after the diagnostic
page verdict; temporary workers were terminated after their result was captured.
That separate teardown behavior was not repaired by this patch. Main-browser
restart and device reboot were verified.

Source patches, build/usage instructions and rollback are under
[sources/mesa/fd506-gmem](../../../sources/mesa/fd506-gmem/README.md).
