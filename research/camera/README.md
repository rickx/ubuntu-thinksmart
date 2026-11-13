# Camera Research

This directory is the public-safe camera subset for the `ubuntu-thinksmart` repo.

## Current Status

- live hardware identifies Samsung S5KC505A as the active front sensor
- canonical sensor ID is `0x5034` at `0x0000/0x0001`
- the vendor descriptor strongly matches 7-bit I2C address `0x37`
- the sensor only answers during a powered probe window; a passive Ubuntu-side read currently sees it unpowered
- A driver, symbolic board DTS patch, GPU SoftISP fixes and capture/calibration tools are now available in [sources/camera](../../sources/camera/README.md). Reported streaming is on the Debian-based ThinkSmart Clean 2026.11.01 derivative; the upstream Ubuntu 24.04 image remains unverified and needs a clang-18/KCFI module rebuild.

## What Is Included Here

- current driver plan in `analysis/s5kc505a-driver-plan.md`
- implementation and remaining limits in [s5kc505a-driver-notes.md](analysis/s5kc505a-driver-notes.md)
- reference tuning and flat-field workflow in [isp-tuning-notes.md](analysis/isp-tuning-notes.md)
- real-time pipeline and LSC statistics in [libcamera-softisp-notes.md](analysis/libcamera-softisp-notes.md)
- reverse-engineering notes in `analysis/s5kc505a-reverse-notes.md`
- supporting public-safe inventories in `inventories/`

The analysis published here reflects the current reverse-engineering results. The repo omits only proprietary inputs and bulk artifact dumps that are not suitable for publication.

## Key Findings So Far

- `libmmcamera_s5kc505a.so` exposes the useful bring-up symbols `sensor_open_lib`, `s5kc505a_calculate_exposure`, and `s5kc505a_fill_exposure_array`
- `sensor_open_lib` resolves to the in-blob `s5kc505a` descriptor, which is enough to anchor descriptor decoding instead of treating the blob as opaque
- the active exposure path emits writes for `0x0340/0x0341`, `0x0202/0x0203`, `0x0204/0x0205`, and the repeated digital-gain block `0x020e` through `0x0215`
- the blob tail and chromatix mapping now strongly point to five real mode entries, including snapshot/ZSL and HFR 60/90/120 splits
- no dedicated S5KC505A EEPROM module was recovered by name from the vendor stack, so EEPROM handling is still an open question

See `analysis/s5kc505a-reverse-notes.md` for the function-level reasoning behind those conclusions.

## What Is Intentionally Not Included

The proprietary Android vendor camera blobs used during reverse engineering are not copied into this repo by default.

Those binaries are not included in this repo.

The repo keeps only the narrower public-safe inventories that help driver work directly.
