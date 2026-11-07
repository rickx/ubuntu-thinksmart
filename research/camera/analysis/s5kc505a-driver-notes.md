# S5KC505A driver and board integration

SPDX-License-Identifier: CC-BY-4.0

## Scope and evidence

The implementation in `sources/driver/s5kc505a/` advances the earlier
[driver plan](s5kc505a-driver-plan.md). Reported hardware validation is on
ThinkSmart Clean 2026.11.01, a Debian trixie derivative with
`6.19.5-thinksmart+`, GCC and no KCFI. It is not validation of this repository's
Ubuntu 24.04/KCFI image. This contribution was assembled and statically
checked in WSL without accessing the device.

The driver was recovered from the WSL camera-check source tree used to build
the derivative image's external module, then extended with Bayer-aware flips
and an option to hide them. Register lists are decoded numeric data from
Lenovo's proprietary `libmmcamera_s5kc505a.so`, not GPL sources found in its
published archives. See [provenance](../../../sources/camera/NOTICE.md).

## Electrical description and sequence

The active boot DTB establishes CCI bus 0, address `0x37`, reset GPIO40 and
powerdown GPIO39, both active-low. The older research hypothesis that GPIO39
was active-high is superseded by that working DTB and this driver's logical
GPIO sequence. The source kernel labels this bus `cci_i2c0`; some trees call
it `cci0_i2c0`. Use symbolic labels, not decompiled numeric phandles.

| Supply/clock | Connection |
| --- | --- |
| VIO | PM8953 L6, 1.8 V |
| VANA | Fixed 2.8 V rail, enable GPIO119, fed by L17 |
| VDIG | Fixed 1.2 V rail, enable GPIO118, fed by L2 |
| External clock | CAMSS MCLK0 / GPIO26, 24 MHz |
| CCI pins | GPIO29/30, cci_i2c function |
| CSI | Two lanes, 480 MHz link / 960 Mbit/s per lane, CSIPHY0 |

Power-on asserts reset and powerdown logically, enables all three supplies,
waits 1–2 ms, starts MCLK, waits 1–2 ms, releases powerdown, waits 1–2 ms,
then releases reset and waits 20 ms before identification. The two fixed
regulators additionally specify 1000 us startup delays. Power-off asserts
both lines, stops MCLK and disables the supplies. Runtime PM uses autosuspend.
The known-good mode tables program 24 MHz; the old plan's 19.2 MHz candidate
was not the final working choice.

The canonical ID is `0x5034` at `0x0000/0x0001`, with `0x300a/0x300b` read
as corroboration. Passive reads while the sensor is unpowered are not a
reliable probe. Board rotation is 180 degrees. Sensor lanes 1/2 connect to
CAMSS physical lanes 0/1 with clock lane 7.

## Modes and controls

The driver advertises packed-compatible RAW10 bus formats: 1304x980 binned
by default, and optionally 2608x1960 with `full_res=1`. Timing uses pixel
rate 190 MHz, line length 3112 and default frame length 2030. These register
timings and end-to-end capture throughput are different quantities; the
full-resolution CPU/offline pipeline is much slower than the binned GPU path.

Exposure is coarse integration at `0x0202`, frame length at `0x0340` and
analog gain at `0x0204`. Analog codes `0x20..0x200` represent code/32,
1–16x. Digital gains at `0x020e/0x0210/0x0212/0x0214` are Gr/R/B/Gb in
8.8 fixed point. Timing/exposure updates use group hold. The driver exposes
exposure, analog/digital gain, blanking, link/pixel-rate, test-pattern and
optional flip controls; it does not implement a calibrated autofocus system.

Color bars and captures establish native GRBG. Register `0x0101` bit 0
mirrors horizontally and bit 1 flips vertically; the Bayer phase changes:

| HFLIP | VFLIP | Bus order |
| --- | --- | --- |
| 0 | 0 | GRBG |
| 1 | 0 | RGGB |
| 0 | 1 | BGGR |
| 1 | 1 | GBRG |

The controls carry MODIFY_LAYOUT and are grabbed during streaming. Format
selection tracks their values. `bayer_order` remains an explicit override;
default is 1 (GRBG). `flip_ctrls=0` skips creation of both controls and all
uses tolerate absent pointers. Their default presence works with libcamera
0.8, which uses both for the board rotation. Libcamera 0.4 changes the subdev
Bayer order but retains its pre-transform capture format; CAMSS link
validation fails with Broken pipe. Hide the controls or use the newer stack.

## Calibration limits

Two CD-18781Y GPL packages contain Linux 3.18.71 camera_v2 transport and board
power descriptions, but no sensor-specific userspace library/OTP decoder.
The ThinkSmart View Plus archive is another platform (4.19.81); it is not a
source for this sensor. The CD board camera node omits an EEPROM name/fixed
memory map; generic msm_eeprom accepts userspace-provided transactions. Its
binding examples do not establish this module's OTP field layout.

The generic Samsung M2M material describes 64-byte OTP pages and a possible
6x6x4-channel, 20-bit LSC SRAM interface. It does not specify an S5KC505A
module's channel order, flags, AWB offsets or checksum. A local read showed
a candidate 360-byte region across pages 3–9. Several packings loaded into
volatile SRAM failed to improve shading. That region is a hypothesis,
not a supported calibration decoder. No OTP programming is implemented or
required. The experiment helper excludes the OTP-program command from its
write whitelist, but it still modifies volatile registers and is opt-in.

Use external flat-field calibration for now. No single-unit OTP dump or
residual is committed. Remaining integration work includes a DT binding,
upstream KCFI module build/boot validation, broader suspend/mode/stream-error
testing and review of the provenance of recovered numeric tables.
