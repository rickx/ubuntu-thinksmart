# Sensor Research

This directory covers proximity, ambient-light, and any other non-camera sensors on the
Lenovo ThinkSmart View CD-18781Y that are not yet ported or are partially ported in the
Ubuntu image.

## Hardware Summary

### VCNL4200 — Proximity / ALS (WORKING in Ubuntu)

- I2C address: `0x51`
- Bus: `i2c-0` (GPIO518/519 bit-bang bus, **not** a QCOM QUP I2C controller)
- Status: Supported via userspace proximity-wake daemon in the Ubuntu port
- Reference: `ubuntu-thinksmart/README.md` (proximity wake section)

### BMA25x Accelerometer Family (Stock Android Runtime)

- `dumpsys sensorservice` reports `BMA255 Accelerometer/Temperature/Double-tap`
- `sensor_def_qcomdev.conf` section labels the corresponding config block as `BMA253`
- Practical interpretation: this is likely a shared BMA25x family path with variant
	naming differences between static config comments and runtime HAL strings

### I2C 3-0014 — Goodix Touch Option Node (Not ALS/PRX)

- Bus: `i2c-3` (QCOM QUP controller at `0x78b7000`)
- I2C address: `0x14`
- Driver bound on stock Android: **none**
- DT node name on stock Android: `gt9xx-i2c@14`
- Interpretation: optional Goodix GT9xx touchscreen controller population
- Current role on ThinkSmart unit: fallback touch controller node, not active

### I2C 3-0038 — FocalTech Touch Option Node (Not ALS/PRX)

- Bus: `i2c-3` (QCOM QUP controller at `0x78b7000`)
- I2C address: `0x38`
- Driver bound on stock Android: **none**
- DT node name on stock Android: `focaltech@38`
- Interpretation: optional FocalTech touchscreen controller population
- Current role on ThinkSmart unit: fallback touch controller node, not active

### I2C 3-0048 — Himax Touch Controller (Active)

- Bus: `i2c-3` (QCOM QUP controller at `0x78b7000`)
- I2C address: `0x48`
- Driver bound on stock Android: `himax_tp`
- DT node name on stock Android: `himax_ts@48`
- Role: active touchscreen controller on this unit

## Sensor HAL References Found in vendor conf

`/vendor/etc/sensors/sensor_def_qcomdev.conf` on the stock device lists configuration
blocks for the following chips (these are platform-generic entries, not all are
necessarily populated on this board):

- **LTR556** — Lite-On proximity/ALS (SSI SMGR Cfg 1)
- **VCNL4200** — Vishay proximity/ALS (SSI SMGR Cfg 1, confirmed working)
- **TMG490X** — AMS proximity/ALS (SSI SMGR Cfg 3 and 4)
- **APDS9960** — Broadcom proximity/RGB-ALS (SSI SMGR Cfg 4, 5, and 6)
- **APDS9950** — Broadcom proximity/ALS (SSI SMGR Cfg 4 and 5)
- **ISL29147** — Renesas ALS (override entry)

## Open Questions

1. Which SSC/ADSP transport exposes VCNL4200 to userspace on stock Android?
2. How do SSC bus numbers map to Linux-visible I2C numbering for this board?
3. Are the static `BMA253` config label and runtime `BMA255` name just family aliases,
   or does this board vary by accelerometer population across production lots?

## Next Steps

- Examine the SSC ADSP sensor descriptors and bus mapping for VCNL4200 and BMA253
- Try reading chip ID registers over ADB via i2c-dev (requires root or a rooted boot)
- Check if a bootloader/fastboot diagnostic mode exposes I2C scan results
- Cross-reference PCB photos or FCC filing internal photos for populated IC markings
- Search Qualcomm MSM8953 reference DTs for SSC sensor bus numbering hints
