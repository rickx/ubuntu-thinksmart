# S5KC505A Reverse Notes

Date: 2026-05-21

## Scope

This note records the first focused reverse-engineering pass over the extracted Lenovo vendor camera stack for the confirmed front sensor on the CD-18781Y.

Files inspected:

- `vendor_blobs/lib/libmmcamera_s5kc505a.so`
- `vendor_blobs/lib/libchromatix_s5kc505a_common.so`
- `vendor_blobs/lib/libchromatix_s5kc505a_preview.so`
- `vendor_blobs/lib/libchromatix_s5kc505a_snapshot.so`
- `vendor_blobs/lib/libchromatix_s5kc505a_default_preview_3a.so`
- `vendor_blobs/lib/libchromatix_s5kc505a_default_video_3a.so`
- `vendor_blobs/lib/hw/camera.msm8953.so`
- `vendor_blobs/bin/mm-qcamera-daemon`
- `vendor_blobs/etc/camera/camera_config.xml`
- `vendor_blobs/etc/camera/s5kc505a_chromatix.xml`

## Sensor Blob

`libmmcamera_s5kc505a.so` is a 32-bit ARM EABI5 shared object with three exported symbols that matter for bring-up:

- `s5kc505a_calculate_exposure`
- `s5kc505a_fill_exposure_array`
- `sensor_open_lib`

Observed properties:

- `sensor_open_lib` returns a pointer into the blob's `.data` section.
- The returned descriptor begins with the ASCII sensor name `s5kc505a`.
- `s5kc505a_calculate_exposure` rejects a null output pointer and clamps one integer gain path to the range `0x20` through `0x1ff`.

Function-level anchor:

- `sensor_open_lib` is a tiny Thumb stub that loads a PC-relative literal `0x3748`, adds it to the current PC, and returns exact virtual address `0x4008`
- with `.data` at virtual address `0x4000` and file offset `0x3000`, that maps exactly to file offset `0x3008`, which is the already confirmed descriptor start

Cross-sensor descriptor check:

- the live descriptor for `libmmcamera_s5kc505a.so` starts at file offset `0x3008`
- the first 32-bit field after the padded sensor name is `0x0000006e`
- the same field is `0x0000006c` in `libmmcamera_ov5693.so`
- the same field is `0x0000005a` in `libmmcamera_s5k4e8.so`

This is a strong match for the vendor sensor-library slave-address field being stored as the 8-bit I2C address:

- S5KC505A: `0x6e` -> 7-bit address `0x37`, which matches the live hardware probe
- OV5693: `0x6c` -> 7-bit address `0x36`, which matches the common OV5693 address family
- S5K4E8: `0x5a` -> 7-bit address `0x2d`, which is plausible for that part family

That means the descriptor decode is now anchored on one confirmed field rather than only on the leading sensor-name string.

Additional descriptor words now line up with the Qualcomm camera enums in the Lenovo kernel headers:

- descriptor offset `0x302c` is `0x00000001` in all three blobs, which matches `I2C_FAST_MODE`
- descriptor offset `0x3030` is `0x00000002` in all three blobs, which matches `MSM_CAMERA_I2C_WORD_ADDR`

That is consistent with the hardware-side conclusion that these sensors are configured for fast-mode I2C and 16-bit register addresses.

One nearby field is now partially decoded. At descriptor offset `0x3038`:

- `libmmcamera_ov5693.so` contains `0x0a 0x30 0x90 0x56`, which is consistent with `(id_reg = 0x300a, sensor_id = 0x5690)`
- `libmmcamera_s5k4e8.so` contains `0x00 0x00 0xe8 0x04`, which is consistent with `(id_reg = 0x0000, sensor_id = 0x04e8)` and matches the Samsung-style `0x0000` ID register family
- `libmmcamera_s5kc505a.so` contains `0x00 0x00 0x34 0x50`, which would decode the same way as `(id_reg = 0x0000, sensor_id = 0x5034)`

That makes it much more likely that this field is a packed `(sensor_id_reg_addr, sensor_id)` pair stored as little-endian 16-bit halves.

The adjacent zero word is also meaningful in Qualcomm terms:

- descriptor offset `0x303c` is `0x00000000` in all three blobs
- Lenovo's kernel headers define `struct msm_sensor_id_info_t` as `(sensor_id_reg_addr, sensor_id, sensor_id_mask)`
- taken together, offsets `0x3038..0x303c` are a good fit for `sensor_id_info = { reg_addr, sensor_id, mask=0 }`

The next words also show a useful cross-check against `camera_config.xml`:

- descriptor offset `0x3040` is `0x00000002` for `ov5693` and `0x00000001` for both `s5k4e8` and `s5kc505a`
- that matches the XML `ModesSupported` split exactly: ov5693 uses `2`, while the two front-camera Samsung entries use `1`

One later field is also a good candidate for sensor position:

- descriptor offset `0x30a4` is `0x00000000` for `ov5693`
- the same field is `0x00000001` for both `s5k4e8` and `s5kc505a`

That matches the XML front/back split (`BACK=0`, `FRONT=1`) and is therefore a plausible position field, although it is not yet proven the way the address and addr-type fields are.

The post-ID descriptor tail is also informative by itself:

- `s5k4e8` and `s5kc505a` are byte-identical through the currently dumped post-ID region
- `ov5693` diverges exactly in the places where `camera_config.xml` says the module properties differ (front/back and `ModesSupported`)

That strongly suggests this block is carrying real module metadata and not just opaque sensor-private state.

Important boundary: this blob is not a raw `struct msm_camera_sensor_slave_info` dump from the kernel headers. The vendor block starts with only a single 32-byte sensor name, while the kernel struct begins with five consecutive 32-byte names (`sensor`, `eeprom`, `actuator`, `ois`, `flash`) before the scalar fields. So the early scalar decodes (`slave_addr`, `i2c_freq_mode`, `addr_type`, `sensor_id_info`) are strong, but later field placement should still be treated as pattern matching until a userspace `sensor_lib_t` definition or a function-level cross-reference pins the layout down.

Powered live read on 2026-05-22 resolved the `0x5034` versus `0x5203` conflict:

- first powered read window: `0x300a/0x300b -> 0x5203`, while `0x0000/0x0001` still NACKed
- second powered read window 200 ms later: `0x0000/0x0001 -> 0x5034`
- the same probe also still showed `0x300a/0x300b -> 0x5203`
- the temporary OV5693 probe module logged `sensor ID mismatch. Got 0x5203`

So the conflict is no longer real: the canonical sensor ID is `0x5034` at `0x0000/0x0001`, and `0x5203` is a different value exposed at `0x300a/0x300b`. That also explains why the OV5693 test module reported `0x5203` without that value being the real Samsung chip ID.

This powered probe also showed a small timing nuance that matters for driver detect logic:

- `0x300a/0x300b` became readable slightly before `0x0000/0x0001`

That supports a probe strategy which treats `0x300a/0x300b = 0x5203` only as corroboration, then retries the canonical `0x0000/0x0001` ID once after a short delay.

Current live-device state check:

- a read-only probe on Ubuntu, with no module loads and no DT changes, showed that bus `3` currently does **not** ACK address `0x37`
- direct reads of `0x0000` and `0x300a` both failed with `No such device or address`
- this is consistent with earlier findings that the sensor only answers during a powered probe window; the current system state leaves it unpowered

## Downstream DTS Power-Enable Evidence

Lenovo's downstream `apq8053-lite-dragon-v2.0.dtsi` adds two extra GPIO entries to both `&eeprom0` and `&camera0`:

- GPIO `118` labeled `CAM_VDIG`
- GPIO `119` labeled `CAM_VANA`

That matters because it is direct board-level evidence that the camera rails are not purely passive PMIC supplies in the shipping downstream design. At least one Lenovo board DTS variant explicitly gates the camera digital and analog rails with GPIO-controlled enables.

This is the cleanest explanation so far for the current Ubuntu-side behavior where bus `3` does not ACK `0x37` during a passive read: the sensor can remain fully unpowered until those rail enables are asserted as part of the downstream camera power sequence.

Current working interpretation:

- candidate digital-rail enable GPIO: `118`
- candidate analog-rail enable GPIO: `119`
- these are strong bring-up candidates for first mainline probe attempts

Important boundary:

- the downstream instance naming is messy; the useful override is attached to `&eeprom0` and `&camera0`, not to a cleanly named `front` sensor node
- treat this as strong rail-enable evidence, not yet as proof that every downstream camera index maps 1:1 to the active S5KC505A instance
- the GPIO evidence is stronger than the exact downstream regulator-wrapper naming or any `regulator-always-on` policy

## Active Exposure Register Writes

The Thumb disassembly of `s5kc505a_fill_exposure_array` shows that the active path writes these register groups:

- `0x0340` and `0x0341`
- `0x0202` and `0x0203`
- `0x0204` and `0x0205`
- `0x020e` and `0x020f`
- `0x0210` and `0x0211`
- `0x0212` and `0x0213`
- `0x0214` and `0x0215`

Reasonable first-pass interpretation:

- `0x0340/0x0341`: frame length lines or equivalent frame-timing register pair
- `0x0202/0x0203`: coarse integration time
- `0x0204/0x0205`: analog gain
- `0x020e` through `0x0215`: repeated digital-gain words, likely one 16-bit value replicated across four color channels
- the write pattern is byte-split across consecutive addresses, which strongly suggests 16-bit register addresses with 8-bit register data in the Linux driver

The function emits those writes as `(register, value, delay)` triplets into the Qualcomm sensor register-setting array.

## Fixed Register Tables

`s5kc505a_fill_exposure_array` also references two optional fixed register-sequence tables stored in `.data`.

Current state in this blob:

- the first table count is zero at runtime, so its sequence is skipped
- the second table count is zero at runtime, so its sequence is also skipped

One visible dormant pair in the first table region is `0x0104 = 0x0001`, which looks like a candidate group-hold enable write, but it is not active in the current blob because the associated count is zero.

The disassembly now pins that more tightly:

- `s5kc505a_fill_exposure_array` uses the same descriptor-relative base `0x4008` as `sensor_open_lib`
- the optional fixed-table loop reads halfwords from `base + 0x0ac4` and `base + 0x0ac6` with an `index * 8` stride
- that proves the dormant entries are stored inline as `u16 reg`, `u16 value`, `u32 delay`
- the first dormant Samsung entry at file offset `0x3acc` is therefore unambiguously `0x0104 = 0x0001` with delay `0`

Nearby low-data helper blocks now look like small inline reg-setting structs rather than arbitrary scalars:

- file offset `0x3790`: header words `2, 1, 0` followed by packed word `0x00000100`, which decodes cleanly as `0x0100 = 0x0000`; this block is identical in all three blobs and is a strong stream-off helper candidate
- file offset `0x3ac0`: header words `2, 1, 0` followed by packed word `0x00010104`, which decodes as `0x0104 = 0x0001`; this block is identical in `s5kc505a` and `s5k4e8` but differs in `ov5693`, which makes it a strong Samsung group-hold-on helper candidate
- file offset `0x3df0`: header words `2, 1, 0` followed by packed word `0x00000104`, which decodes as `0x0104 = 0x0000`; this is the matching Samsung group-hold-off helper candidate
- file offset `0x3468`: a small nearby block contains count word `1` followed by packed word `0x00010100`, which decodes as `0x0100 = 0x0001`; this is a plausible stream-on helper, although the surrounding metadata is not yet fully decoded

Cross-sensor note:

- the `0x0100 = 0x0000` block at `0x3790` is shared by all three blobs
- the `0x0104 = 0x0001` and `0x0104 = 0x0000` blocks are shared by the two Samsung front-sensor blobs and differ in `ov5693`

That split is exactly what we would expect if these are genuine stream/group-hold helper settings rather than unrelated counters.

## High-Data Tail

One high-data region now has a stronger structural decode than before.

At virtual address `0x79200` (file offset `0x78200`):

- `libmmcamera_s5kc505a.so` stores first dword `0x00000005`
- `libmmcamera_s5k4e8.so` stores first dword `0x00000005`
- `libmmcamera_ov5693.so` stores first dword `0x00000002`

That does **not** match XML `ModesSupported`, but it does match the chromatix resolution-index count exactly:

- `s5kc505a_chromatix.xml` defines indices `0..4` -> count `5`
- `ov5693_chromatix.xml` defines indices `0..1` -> count `2`

This makes the first dword at `0x79200` a strong candidate for a true resolution-count or mode-count field in the sensor-library tail.

The same high-data tail also contains two dynamic relocations at virtual addresses `0x7920c` and `0x79210`:

- `0x7920c` -> `s5kc505a_calculate_exposure`
- `0x79210` -> `s5kc505a_fill_exposure_array`

So the region around `0x79200` is very likely part of the `sensor_lib` tail that binds resolution-count metadata to the exposure helper callbacks.

What is still missing there is the exact pointer or inline layout for the per-mode output and init arrays. But the count field is no longer a blind guess.

## First Real Mode-Table Candidates

The `.data` sweep now exposes the first strong per-mode register-table candidates instead of only helper blocks.

Five large non-zero blocks appear at these file offsets:

- `0x3ee0`
- `0x42ee0`
- `0x46ef0`
- `0x4af00`
- `0x4ef10`

All five blocks share the same general shape:

- leading scalar header words
- then a long run of packed `u32` words that decode cleanly as `low16 = reg`, `high16 = value`

Representative packed writes from the first block:

- `0x00180136` -> `0x0136 = 0x0018`
- `0x00060301` -> `0x0301 = 0x0006`
- `0x00040305` -> `0x0305 = 0x0004`
- `0x005f0307` -> `0x0307 = 0x005f`
- `0x0004030d` -> `0x030d = 0x0004`
- `0x00a0030f` -> `0x030f = 0x00a0`
- `0x00033c17` -> `0x3c17 = 0x0000` in this mode block, with other mode blocks changing the same late-register region
- `0x00030820` / `0x00c00821` -> `0x0820 = 0x0003`, `0x0821 = 0x00c0`

That is exactly the kind of packed `reg/value` body expected from sensor init or per-mode register arrays. These are no longer hypothetical table pointers; they are concrete candidate mode register tables already sitting in the blob.

The stronger high-data mode summary is at file offset `0x76fc0`:

- dword at `0x76fc4` is `5`
- five fixed-size records follow at `0x76fc8`, `0x77008`, `0x77048`, `0x77088`, and `0x770c8`

Those five records contain plausible packed geometry/timing fields. Interpreting each dword as two 16-bit little-endian halves gives these candidate active sizes:

- mode 0: `2608 x 1960`
- mode 1: `1304 x 980`
- mode 2: `1936 x 1096`
- mode 3: `1296 x 736`
- mode 4: `640 x 490`

The next packed timing word in the same records also looks plausible as line-length / frame-length style data:

- mode 0: `3112 x 2030`
- mode 1: `3112 x 2030`
- mode 2: `2760 x 1166`
- mode 3: `2760 x 806`
- mode 4: `2800 x 565`

These records line up with the already suspected five-mode split from `s5kc505a_chromatix.xml` much better than anything else seen in the blob.

There is also a second count-5 block at file offset `0x77340`:

- dword at `0x77348` is `5`
- repeated per-mode records begin immediately after it
- those records contain packed values like `0x00001302` and `0x00022b00`

The `0x2b` byte is a strong RAW10-style clue, so this second block is a good candidate for CSI or output-format metadata that parallels the five mode records at `0x76fc8`.

Current best interpretation:

- low `.data` helper islands around `0x3460`, `0x3790`, `0x3ac0`, and `0x3df0` hold tiny shared reg-setting helpers such as stream-off and Samsung group-hold toggles
- the five large blocks from `0x3ee0` through `0x4ef10` are the first strong candidates for full per-mode register tables
- the five fixed-size records at `0x76fc8` look like the matching per-mode output/timing summary
- the count-5 block at `0x77340` is a likely companion CSI/format table

The remaining work is to pin the linking metadata between those three regions rather than to prove that the regions exist.

## Chromatix Split

The representative S5KC505A chromatix blobs each export only one symbol:

- `load_chromatix`

They do not expose additional readable mode metadata internally. The useful mode split comes from filenames and from `s5kc505a_chromatix.xml`.

Confirmed resolution-to-tuning mapping from XML:

- `sensor_resolution_index="0"`: snapshot and ZSL family
- `sensor_resolution_index="1"`: snapshot and ZSL family again
- `sensor_resolution_index="2"`: HFR 60 family
- `sensor_resolution_index="3"`: HFR 90 family
- `sensor_resolution_index="4"`: HFR 120 family

Confirmed tuning families on disk:

- common: `libchromatix_s5kc505a_common.so`
- ISP: preview, snapshot, HFR 60, HFR 90, HFR 120
- CPP: preview, snapshot, video, liveshot, HFR 60, HFR 90, HFR 120
- 3A: default preview, default video, ZSL preview, ZSL video, HFR 60, HFR 90, HFR 120

## EEPROM Hunt

What was checked:

- exact filenames in the extracted vendor payload
- strings in `libmmcamera_s5kc505a.so`
- strings in `camera.msm8953.so`
- strings in `mm-qcamera-daemon`
- broad grep across the extracted `vendor_blobs/` tree

What was found:

- `camera_config.xml` contains `EepromName=qtech_s5kc505a`
- no `libmmcamera_s5kc505a_eeprom.so`
- no `libmmcamera_qtech_s5kc505a_eeprom.so`
- no direct `s5kc505a.*eeprom`, `otp.*s5kc505a`, or `calib.*s5kc505a` hits in the inspected binaries

Important nuance:

- the vendor stack does contain generic EEPROM plumbing such as `libmmcamera_eeprom_util.so`, `libmmcamera_eebinparse.so`, and many other sensor-specific `_eeprom.so` modules
- this makes the S5KC505A absence look sensor-specific, not like a broken extraction or a missing generic subsystem

Best current interpretation:

- there is no standalone S5KC505A EEPROM blob under an obvious filename
- calibration may be absent, bundled into another binary, or named in a way that does not mention `s5kc505a`

## Immediate Next Reverse Path

1. Decode the descriptor returned by `sensor_open_lib` far enough to recover sensor ID, slave address, and mode-table pointers.
2. Recover the caller argument meanings for `s5kc505a_fill_exposure_array` so the register writes can be mapped cleanly to frame length, integration time, analog gain, and digital gain.
3. Search `camera.msm8953.so` for the code path that resolves `EepromName` into a shared-object filename, since that may explain the missing dedicated blob.

## Driver Direction

Best current direction for a new Linux driver:

- use a small mainline-style V4L2 subdev structure modeled first on `kernel_references/lineage-19.1/drivers/media/i2c/s5k6a3.c`
- borrow Samsung-specific register-access ideas from `kernel_references/lineage-19.1/drivers/media/i2c/s5k4ecgx.c` and `kernel_references/lineage-19.1/drivers/media/i2c/s5k5baf.c`
- start with 16-bit register addresses and 8-bit register values
- treat the known exposure path as the first control slice to implement because the vendor blob gives concrete register targets already