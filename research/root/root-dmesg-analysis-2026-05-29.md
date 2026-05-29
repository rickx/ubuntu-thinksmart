# Root dmesg Analysis — 2026-05-29

Device: Lenovo ThinkSmart View CD-18781Y  
Method: Magisk v30.7, boot image patched via EDL  
Root confirmed: `uid=0(root) gid=0(root) context=u:r:magisk:s0`

Raw logs in `android-root/`:
- `dmesg.txt` — full kernel log
- `dmesg_camera.txt` — filtered: camera/CCI/sensor
- `dmesg_sensors.txt` — filtered: SLPI/SSC/sensor
- `dmesg_audio.txt` — filtered: TAS5782M/audio
- `debugfs_root.txt` — `/sys/kernel/debug/` listing

## Camera

CCI hardware: `hw_version=0x10020005`, `CSID_VERSION=0x30050001`

Probe sequence (from `dmesg_camera.txt`):

| Sensor | Result |
|--------|--------|
| OV5693 | `read id failed` (rc -22, 3 attempts) → `power up failed` |
| S5K4E8 | `read id failed` (rc -22, 3 attempts) → `power up failed` |
| **S5KC505A** | **`probe succeeded`** |

**S5KC505A is the actual sensor.** All prior research in `research/camera/` is correct.

Action required: ubuntu-thinksmart DTS patch targets OV5693 at `cci_i2c0@36` — must be updated to
S5KC505A at `@37` once the driver is written.

## Sensors (SLPI/SSC)

SLPI firmware load fails at boot:

```
sensors-ssc: slpi_load_fw: pil get failed,
sensors-ssc: slpi_load_fw: SLPI image loading failed
```

Result: VCNL4200 ALS/PRX and BMA253 accelerometer are inaccessible on stock Android via the SSC path.
These sensors are connected to the SLPI DSP's I2C master, not the AP I2C buses.

On Ubuntu: either load the SLPI firmware image or drive VCNL4200/BMA253 directly from AP if
the physical I2C bus is also wired to an AP I2C controller (needs further DTS research).

Himax touchscreen confirmed working: `sensor_id=2602`, `himax_loadSensorConfig: initialization complete`.

## Audio

TAS5782M probe sequence:

```
[2.429] [tas5782m_probe]
[2.554] [tas5782m_probe] gpio_direction_output ok
[2.554] [tas5782m_probe] snd_soc_register_codec begin
[5.773] msm8952-asoc-wcd c051000.sound: snd_soc_register_card failed (-517)  <- EPROBE_DEFER, QDSP6 not ready
[5.960] TAS5782M: tas5782m_snd_probe  <- second attempt
[5.990] input: msm8953-snd-card-mtp Headset Jack ...
[5.990] input: msm8953-snd-card-mtp Button Jack ...
```

Card `msm8953-snd-card-mtp` registers cleanly on second attempt.

TAS5782M confirmed at:
- I2C bus: `i2c@78b5000` (AP bus 1, `i2c_1` in DTS)
- I2C address: **`0x49`** (`OF_FULL_NAME=/soc/i2c@78b5000/tas5782m@49`)
- ASoC codec name: `tas5782m.1-0049`

## I2C Bus Map

| Bus | DTS name | Devices |
|-----|----------|---------|
| i2c-1 (`i2c@78b5000`) | `i2c_1` | TAS5782M @ 0x49 |
| i2c-3 (`i2c@78b7000`) | `i2c_3` | Himax @ 0x48, unbound fallbacks @ 0x14, 0x38 |

VCNL4200 (0x51) and BMA253 (0x18) are on SSC I2C (not AP-visible without SLPI firmware).

## Video Nodes

| Node | Purpose |
|------|---------|
| video0, video1 | MSM camera capture |
| video32, video33 | VIDC hardware video codec (enc/dec) |
| v4l-subdev0..18 | Camera subdevices (CCI, CSIPHY, CSID, VFE, sensor) |

## debugfs Accessible as Root

`/sys/kernel/debug/` nodes available: `camera`, `asoc`, `msm_isp0`, `msm_isp1`,
`msm_vidc`, `clk`, `regulator`, `gpio`, `mmc0`, `mmc1`, `kgsl`, `mdp`, `pinctrl`, `tracing`, `iommu`
