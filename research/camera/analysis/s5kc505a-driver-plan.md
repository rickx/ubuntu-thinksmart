# S5KC505A Driver Plan

Date: 2026-05-21

## Goal

Build a new Linux V4L2 subdevice driver for the Samsung S5KC505A front sensor used on the Lenovo ThinkSmart View CD-18781Y.

This is not a Qualcomm camera-HAL rehost plan. The target is a native Linux sensor driver that can probe the chip, expose one or more modes, and participate in a standard media pipeline.

## Why This Is The Right Goal

Current state:

- the sensor identity is confirmed on hardware: canonical ID `0x5034` at `0x0000/0x0001`, with corroborating `0x5203` at `0x300a/0x300b`
- the live sensor answers at I2C address `0x37`
- the extracted vendor stack contains `libmmcamera_s5kc505a.so` and the full `libchromatix_s5kc505a_*` family
- the vendor blob already reveals the active exposure register path

That is enough to start a driver in a disciplined way instead of waiting for a perfect dump of every vendor table.

## Local Reference Drivers

Primary structural scaffold:

- `kernel_references/lineage-19.1/drivers/media/i2c/s5k6a3.c`

Why:

- small and close to a mainline-style V4L2 subdev driver
- shows clean power sequencing, pad operations, and basic format handling
- a good template for the first probe-only or single-mode version

Samsung register-access references:

- `kernel_references/lineage-19.1/drivers/media/i2c/s5k4ecgx.c`
- `kernel_references/lineage-19.1/drivers/media/i2c/s5k5baf.c`

Why:

- both demonstrate Samsung sensor access patterns
- both use 16-bit register addressing
- both show the `0xfcfc` page-select style used by several Samsung parts

Important caution:

- `s5k4ecgx` and `s5k5baf` are ISP-heavy embedded-firmware sensors and should not be copied mechanically
- they are reference material for register access style and Samsung conventions, not a behavior match

## Concrete Driver Assumptions

Current working assumptions for a first implementation:

- bus: I2C
- slave address: `0x37`
- chip ID registers: `0x0000` and `0x0001`
- expected chip ID: `0x5034`
- corroborating status read: `0x300a/0x300b -> 0x5203`
- register addressing: 16-bit
- register data width: 8-bit
- clock: 19.2 MHz external clock
- reset GPIO: 40, active-low at board level
- power-down GPIO: 39, active-high at board level
- candidate rail-enable GPIOs: `118` for VDIG and `119` for VANA from Lenovo's downstream `apq8053-lite-dragon-v2.0.dtsi`
- CSI: `CSIDCore=0`, `LaneMask=0x7`, `LaneAssign=0x4320` from `camera_config.xml`

The `16-bit address + 8-bit value` assumption is supported by both the live chip-ID reads and the vendor exposure writer, which emits high and low bytes to consecutive addresses.

The current board-power working assumption is that first bring-up will likely need both the ordinary camera supplies and explicit board GPIO enables for the digital and analog rails.

## Minimum Viable Driver

Phase 1 should not try to be feature-complete.

The first useful version should do only this:

1. power sequencing
2. reset and standby handling
3. chip ID probe at `0x0000/0x0001`, optionally logging `0x300a/0x300b`
4. one hard-coded mode table
5. stream on and stream off
6. exposure and gain register writes using the vendor-derived register targets

If Phase 1 probes and can stream a raw test frame, the driver is already materially useful.

## Conservative First Probe Order

For the first Linux driver draft, use a conservative detect sequence instead of assuming the sensor is already powered:

1. request clock, regulators, reset GPIO, power-down GPIO, and candidate rail-enable GPIOs
2. drive reset asserted and keep power-down in its inactive-for-probe blocking state
3. enable the camera supplies, then assert candidate VDIG/VANA enables on GPIO `118` and GPIO `119`
4. start the `19.2 MHz` external clock
5. release power-down, then release reset
6. wait a short settle delay and try `0x300a/0x300b`
7. wait once more, then read the canonical ID at `0x0000/0x0001`, retrying once before giving up
8. treat `0x300a/0x300b = 0x5203` only as corroboration; require `0x0000/0x0001 = 0x5034` for a successful detect

This sequence is still a board-level working hypothesis, not yet a proven hardware trace. The important point is that a passive I2C detect is no longer a credible probe model for this sensor on this board.

## Known Register Targets

From `libmmcamera_s5kc505a.so`, the active exposure path writes:

- `0x0340/0x0341`
- `0x0202/0x0203`
- `0x0204/0x0205`
- `0x020e/0x020f`
- `0x0210/0x0211`
- `0x0212/0x0213`
- `0x0214/0x0215`

Best first-pass mapping:

- frame length: `0x0340/0x0341`
- coarse integration: `0x0202/0x0203`
- analog gain: `0x0204/0x0205`
- digital gains: `0x020e` through `0x0215`

These are the first control hooks that should exist in the driver, even if they are not wired to full V4L2 controls on day one.

## What The Vendor Blob Still Does Not Give Us

Still missing in explicit form:

- a fully decoded standalone mode table
- a fully decoded standalone init sequence
- a dedicated EEPROM module for S5KC505A
- a confirmed Bayer order

That means the first driver version should be honest about scope and should prefer one known-good mode over a broad mode matrix.

## Recommended Implementation Shape

Start with a file shaped like this:

- `struct s5kc505a` with `v4l2_subdev`, pads, supplies, clock, GPIOs, mutex, current mode
- small register helpers for `read8`, `write8`, `write16_be_split` if needed
- `power_on` and `power_off`
- `detect` that reads `0x0000/0x0001` and treats `0x300a/0x300b` only as corroboration
- one mode structure carrying width, height, link frequency, pixel rate, and register list pointer
- `s_stream` that writes init registers, selected mode registers, and stream toggle registers

Start with regmap only if it helps. Plain I2C helpers may be simpler for the first version.

## Suggested Order Of Work

1. create a probe-only skeleton based on `s5k6a3.c`
2. add 16-bit-address and 8-bit-value I2C helpers
3. implement chip-ID detection for `0x5034`
4. add power sequencing and GPIO handling for the board
5. wire one mode table and a basic `s_stream`
6. add exposure and gain writes using the known register groups
7. only then expand into multiple modes or extra controls

## Short-Term Research Still Needed

Before or during the first code draft, keep digging on these points:

1. decode the `sensor_open_lib` descriptor enough to recover mode-table pointers
2. mine `camera.msm8953.so` or related blobs for how the HAL selects mode index `0` versus `1` for the duplicated snapshot/ZSL family
3. confirm Bayer order from vendor metadata or from a first captured raw frame
4. validate the candidate GPIO `118` / GPIO `119` rail-enable path on hardware and pin it into the board DT cleanly
5. validate the current stream-on or stream-off helper candidates on hardware or by broader blob cross-check

## Practical Conclusion

Yes, the goal now is a new driver.

The current evidence is already sufficient to start a probe-first, one-mode Linux driver. The remaining reverse work is not a blocker for the skeleton; it is mainly what will separate a minimal working driver from a polished one.