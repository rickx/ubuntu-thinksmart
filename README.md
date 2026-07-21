# Ubuntu on Lenovo ThinkSmart View (CD-18781Y)

> Custom Ubuntu 24.04 port for the Lenovo ThinkSmart View (CD-18781Y).  
> APQ8053 / MSM8953 SoC · ARM64 · kernel 6.19.5-msm8953  
> **Speaker audio, microphone, Bluetooth, accelerometer, brightness slider, and proximity wake are confirmed working. WiFi setup UI is working and GTK rekey dropouts are fixed with the ath10k_core patch.**

**Plasma 6** (KDE neon 6.7.0) is the recommended and primary image.

---

## Hardware

| Component | Details |
|-----------|---------|
| Device | Lenovo ThinkSmart View CD-18781Y |
| SoC | Qualcomm APQ8053 / MSM8953 |
| RAM | 2 GB LPDDR3 |
| Storage | 8 GB eMMC |
| Display | 8-inch IPS touchscreen, DSI |
| Audio amp | Texas Instruments TAS5782M (I2S Class-D) |
| WiFi/BT | Qualcomm QCA9379 hw1.0 SDIO (ath10k_sdio) |
| Modem | N/A (Teams appliance, no cellular) |

---

## Status

| Feature | Status | Notes |
|---------|--------|-------|
| Boot (EDL → lk2nd → extlinux) | ✅ Working | lk2nd.img in this repo |
| Ubuntu 24.04 userland | ✅ Working | First boot requires on-device WiFi setup before SSH |
| WiFi (ath10k_sdio) | ✅ Working | GTK rekey dropouts fixed by patched `ath10k_core.ko`; warning line may still appear in dmesg but connection stays up |
| WiFi settings UI | ✅ Working | Quick Settings deep-link opens the mobile Wi-Fi module, active connection IP is shown in the list |
| ADSP / Qualcomm DSP | ✅ Working | Started by systemd service at boot |
| Speaker audio (TAS5782M) | ✅ Working | Requires custom driver + WirePlumber policy |
| Plasma Mobile (KDE) | ✅ Working | SDDM, touchscreen |
| Virtual keyboard (Maliit) | ✅ Working | Baked into image; Qt5→Qt6 Wayland environment fix |
| Microphone (DMIC) | ✅ Working | UCM2 HiFi profile |
| Display backlight | ✅ Working | WLED; Plasma Mobile brightness slider working (polkit fix required — see below) |
| Proximity sensor (VCNL4200) | ✅ Working | Userspace daemon, no DTB change; triggers screen wake |
| Camera | 🔲 Research in progress | Live hardware points to Samsung S5KC505A; see `research/camera/` |
| Bluetooth | ✅ Working | QCA UART HCI, firmware loads at boot; A2DP tested with WH202A headset |
| Accelerometer (BMA253) | ✅ Working | `bma253` IIO driver; screen auto-rotation via `iio-sensor-proxy` |
| USB OTG | 🔲 Not tested | |
| Hardware video decode | ✅ Working | Venus V4L2 M2M (`h264_v4l2m2m`, `hevc_v4l2m2m`); H.264 1080p at 1.26× real-time (camera RTSP), HEVC 1080p at 5.7×; **B-frames not supported** — encode source without them (`-bf 0` / `-tune zerolatency`); see `research/video/analysis/venus-decode-findings-2026-06-05.md` |

### WiFi Management

Wi-Fi can be managed from Plasma Mobile Settings (quick settings tile or Settings app).

Note: deep-link/IP-display Wi-Fi UI fixes are tracked as post-install unless confirmed in a clean-flash public image. See [DEPLOYMENT_STATE.md](DEPLOYMENT_STATE.md).

`nmtui` remains available as a fallback:

```bash
sudo nmtui
```

### USB Network Management Access

When testing Wi-Fi UI or network changes, use USB networking as the primary recovery and management path:

```bash
ssh user@172.16.42.1
```

This avoids lockout if Wi-Fi setup or policy changes break remote access.

### WiFi Note

- Live logs identify the WiFi device as `QCA9379 hw1.0 sdio` with firmware `WLAN.NPL.1.6-00163-QCANPLSWPZ-1`.
- Root cause: `-110` key install/remove failures come from the hardware crypto offload path waiting for the HTT security indication completion.
- Fix in use: patched `ath10k_core.ko` swallows `-ETIMEDOUT` in `ath10k_set_key` after warning, preventing mac80211-triggered deauth on GTK rekey.
- Firmware features currently log as `wowlan,ignore-otp,mfp` with no `raw-mode`, so `ath10k_core.cryptmode=1` is not a usable workaround on this firmware.

Detailed implementation and verification: [research/wifi/analysis/ath10k-gtk-rekey-fix-2026-06-05.md](research/wifi/analysis/ath10k-gtk-rekey-fix-2026-06-05.md)

---

## Image

A single Ubuntu image is available:

| Image | Desktop | Status | Download |
|-------|---------|--------|----------|
| `plasma6-ubuntu-qcom-msm8953.img` | Plasma Mobile 6.7.0 (KDE neon) | Current | [MEGA](https://mega.nz/file/Am9iCDLA#qf-MOY8UILP1oC07goBndVa6Co3wozt7jhhh-qa4KaM) |

### Image Configuration Notes

- Plasma 6 from KDE neon 6.7.0 is installed and verified working.
- The KDE neon PPA is pre-configured at the correct pin priority — no manual apt changes needed after flash.
- **DPMS is disabled by default** in `~/.config/powerdevilrc` (`DimDisplayIdleTimeoutSec=0`, `TurnOffDisplayIdleTimeoutSec=0`). This is intentional: Plasma 6 on a DSI display puts the panel into unrecoverable hardware sleep (DRM Off) if any powerdevil display timeout fires. The image includes comprehensive wake handlers:
  - **Active wake:** `idle-blanker.py` (1800s timeout) with SimulateUserActivity D-Bus calls keeps KWin's idle timer reset, preventing independent DRM Off.
  - **Passive wake:** `screen-wake-daemon.py` (new 2026-07-21) monitors touch and accelerometer; on motion/touch, it restores brightness and resets KWin's timer. As a fallback for unrecoverable hardware DRM Off, it restarts the KWin compositor.
  - **Proximity wake:** `vcnl4200-proximity-daemon.py` detects hand approach and wakes display when blanked.
  - Backlight blanking uses `BLANK_BRIGHTNESS=10` (physically dark but leaves DRM connector alive).
  - Do not enable any display timeout in Plasma 6 power settings.
- Virtual keyboard (Maliit) is fixed and integrated: Qt5 maliit runs with `QT_QPA_PLATFORM=wayland` in the Qt6 KWin 6 session, so the keyboard appears automatically on text-field focus. No manual configuration needed.
- All hardware fixes (WiFi rekey patch, I2C fix, rotation daemon, proximity wake) are baked in.

### Injecting WiFi before first boot

The image ships with no WiFi credentials. Use `prepare-ubuntu-ssh-bootstrap-image.sh` to pre-inject your network so the device connects and becomes SSH-able immediately after flash:

```bash
sources/scripts/prepare-ubuntu-ssh-bootstrap-image.sh \
  --src rootfs/plasma6-ubuntu-qcom-msm8953.img \
  --out rootfs/plasma6-ubuntu-mydevice.img \
  --ssid "MyWiFi" \
  --psk "MyPassword"
```

Without WiFi injection, connect manually from the touchscreen on first boot before attempting SSH.

---

## What You Need

- A Lenovo ThinkSmart View CD-18781Y
- A Windows or Linux host with [edl](https://github.com/bkerler/edl) installed
- USB-A to USB-C cable (device side is USB-C)
- The generic bootstrap image `bootstrap-pmos-ssh-generic-qcom-msm8953.img` ([GitHub Release](https://github.com/rickx/ubuntu-thinksmart/releases/tag/bootstrap-2026-05-22); includes matching `.sha256`)
- The Ubuntu image `plasma6-ubuntu-qcom-msm8953.img`; see [Image](#image) above
- `prebuilt/lk2nd.img`
- The GPT layout file in `partitions/ubuntu_layout.sfdisk`

---

## Quick Start — Flashing

The device uses Qualcomm EDL (Emergency Download) mode for flashing. Only EDL can be used here: fastboot is present, but flashing fails because the bootloader is OEM-locked and enforces signed images.

See [FLASHING.md](FLASHING.md) for the full stock-Android to Ubuntu flow.

Short version:

1. Enter Qualcomm EDL mode.
2. Flash `lk2nd` with `python edl.py w boot prebuilt/lk2nd.img`.
3. Build a personalized bootstrap image with `sources/scripts/prepare-pmos-ssh-bootstrap-image.sh` using your WiFi SSID and password:
   ```bash
   sources/scripts/prepare-pmos-ssh-bootstrap-image.sh \
     --src rootfs/bootstrap-pmos-ssh-generic-qcom-msm8953.img \
     --out rootfs/bootstrap-pmos-ssh-mydevice.img \
     --login-password thinksmart \
     --ssid MyWiFiNetwork \
     --psk MyWiFiPassword
   ```
4. Flash the personalized bootstrap image with `python edl.py w system <your-personalized-bootstrap>.img`.
5. Wait for it to join WiFi, then SSH in as `pmos` / `thinksmart` and run `sudo /usr/local/sbin/apply-ubuntu-gpt.sh`.
6. Re-enter EDL.
7. Run `python edl.py w system ubuntu-qcom-msm8953.img` (or `plasma6-ubuntu-qcom-msm8953.img` for Plasma 6).
8. Boot the device and connect to WiFi from the touchscreen before attempting SSH (or pre-inject WiFi — see [Images](#images)).

First-time installation uses a temporary SSH-capable bootstrap image so the GPT can be rewritten from the device before flashing the final Ubuntu image.

Useful links and defaults:

- bootstrap image release asset: [bootstrap-2026-05-22](https://github.com/rickx/ubuntu-thinksmart/releases/tag/bootstrap-2026-05-22)
- Ubuntu image (Plasma 6.7.0): [MEGA](https://mega.nz/file/Am9iCDLA#qf-MOY8UILP1oC07goBndVa6Co3wozt7jhhh-qa4KaM)
- create a personalized bootstrap image with `sources/scripts/prepare-pmos-ssh-bootstrap-image.sh`
- bootstrap login: `pmos` / `thinksmart`, hostname `thinksmarter`
- final Ubuntu login: `ubuntu` / `thinksmart`

## License

- `sources/driver/` and `sources/patches/`: `GPL-2.0-only`
- userspace helper scripts and daemons under `sources/`: `MIT`
- documentation: `CC-BY-4.0`
- third-party reference material and prebuilt artifacts remain under their original upstream licenses

---

## Documentation Scope

This repository is public and user-facing.

- Keep `README.md` focused on tested features, install flow, and known limitations.
- Keep `research/` focused on technical analysis that helps contributors and advanced users.
- Keep private work logs, personal notes, and session journals out of this repository.

System-image versus post-install state is tracked in [DEPLOYMENT_STATE.md](DEPLOYMENT_STATE.md).

---

## Key System Services

These systemd services are required for correct operation and are pre-installed in the image:

| Service | Purpose |
|---------|---------|
| `adsp-start.service` | Starts the Qualcomm ADSP remoteproc after firmware is available |
| `qup-i2c-pinctrl-fix.service` | Restores GPIO2/3 mux to I2C mode after PM autosuspend resets them (required for TAS5782M I2C) |
| `alsa-restore.service` | Restores ALSA mixer state (volume) at boot |
| `display-guard.service` | Ensures display backlight is set at boot |
| `idle-blanker.service` *(user)* | Blanks screen after 30 min of no input (backlight-only, never DRM Off); ALS auto-brightness while active. See [sources/idle-blanker.py](sources/idle-blanker.py). |
| `vcnl4200-proximity-daemon.service` *(user)* | Polls VCNL4200 proximity sensor when screen is off; wakes on approach. See [sources/vcnl4200-proximity-daemon.py](sources/vcnl4200-proximity-daemon.py). |
| `rotation-daemon.service` *(user)* | BMA253 accelerometer → kscreen-doctor screen auto-rotation. See [sources/rotation-daemon.py](sources/rotation-daemon.py). |

---

## Audio

Speaker audio works via the TAS5782M Class-D amplifier on the I2S/QUATERNARY_MI2S path from the Qualcomm QDSP6 DSP.

The setup requires:
1. **Custom kernel module** — `snd-soc-tas5782m-dbg.ko` (built with `CC=clang-18`, KCFI-compatible)
2. **DSP firmware** — `tas5728m_dsp_lenovo_cd-18781y.bin` in `/lib/firmware/`
3. **UCM2 profile** — `Lenovo/cd-18781y/HiFi.conf` enabling the Q6→TAS5782M mixer routing
4. **WirePlumber policy** — `/etc/wireplumber/main.lua.d/99-cd18781y-alsa.lua` forcing `S16LE` format and keeping the ALSA PCM device open between sounds (prevents re-init delay)

> ⚠️ **S16LE must be forced in WirePlumber.** The QDSP6 path on this device accepts S24_LE at the ALSA level but produces near-silent output due to a scaling/alignment issue. S32 is rejected outright. See [AUDIO.md](AUDIO.md) for the full investigation.

Quick test after boot:
```bash
# PipeWire path
pw-play /usr/share/sounds/freedesktop/stereo/audio-volume-change.oga

# Direct ALSA path (bypasses PipeWire)
speaker-test -D hw:0,0 -c 2 -t sine -f 1000 -l 1
```

**Full audio documentation:** [AUDIO.md](AUDIO.md)

---

## Display Brightness Slider

The Plasma Mobile brightness slider works via KDE powerdevil's `org.kde.powerdevil.backlighthelper` KAuth helper.

**Required fix:** The stock Ubuntu polkit policy for `backlighthelper` is missing `allow_any=yes` on the `setbrightness` action and has `allow_inactive=no`. This causes polkit to deny the request when the Wayland session is not flagged "active" by logind, so the helper never dispatches the `setbrightness` Q_SLOT.

**Fix — patch `/usr/share/polkit-1/actions/org.kde.powerdevil.backlighthelper.policy`:**  
In the `org.kde.powerdevil.backlighthelper.setbrightness` action's `<defaults>` block, change:
```xml
<defaults>
   <allow_inactive>no</allow_inactive>
   <allow_active>yes</allow_active>
</defaults>
```
to:
```xml
<defaults>
   <allow_any>yes</allow_any>
   <allow_inactive>yes</allow_inactive>
   <allow_active>yes</allow_active>
</defaults>
```

The fix is validated on development installs. For public-image behavior, treat this as post-install unless marked otherwise in [DEPLOYMENT_STATE.md](DEPLOYMENT_STATE.md). The helper binary at `/usr/lib/kauth/libexec/backlighthelper` is rebuilt from powerdevil 5.27.11 upstream source (the Ubuntu-shipped binary uses `BACKLIGHT_RAW` type detection without the `rawAll` fallback, causing init to fail).

WLED brightness range: 0–4095. Values below ~1200 cause the display to turn off (hardware minimum). The slider covers the visible range.

---

## Screen Blanking and Proximity Wake

### Background — why KDE powerdevil display timeouts are disabled

On Plasma 6 (KDE neon), any powerdevil display timeout — both `DimDisplay` and `TurnOffDisplay` — causes KWin to set the DRM connector to Off at hardware level. On this MSM/DSI panel, the connector cannot be re-enabled without restarting the compositor. There is no lightweight wake path (kscreen-doctor crashes, the dpms sysfs node is read-only in Wayland, SimulateUserActivity has no effect). All powerdevil display timeouts are therefore set to 0.

**Screen blanking and proximity wake are handled by three userspace daemons (systemd services):**

### idle-blanker (screen blanking)

`/usr/local/bin/idle-blanker.py` — deployed as `~/.config/systemd/user/idle-blanker.service`

- Watches all `/dev/input/event*` devices via `select()` for physical input events
- After **30 minutes** of no input, writes `brightness=10` to `/sys/class/backlight/backlight/brightness` (must not be 0 — on Plasma 6, writing 0 triggers DRM Off via KWin; WLED is physically off below ~1200 so 10 is dark but safe)
- Restores brightness=4095 when a physical input event arrives while blanked
- Detects external brightness restores (by proximity daemon or other) and resets its idle timer

Requires a sudoers rule (created by the deploy script):
```
user ALL=(ALL) NOPASSWD: /usr/bin/tee /sys/class/backlight/backlight/brightness
```

### vcnl4200-proximity-daemon (proximity wake)

The VCNL4200 proximity + ambient light sensor is at I2C address `0x51` on `i2c-0` (GPIO518/519 bit-bang bus). **No DTB change needed** — the bus is already live.

> ⚠️ **Do NOT use the upstream `vcnl4000` kernel driver.** It enables the IR LED at full power continuously. The userspace daemon below controls the LED only when the screen is off.

`/usr/local/bin/vcnl4200-proximity-daemon.py` — deployed as `/etc/xdg/systemd/user/vcnl4200-proximity-daemon.service`

- Monitors display state at 2 Hz: screen is considered **OFF** if `brightness <= 10` (idle-blanker blanked) **or** `bl_power=4` (legacy DPMS path). Both paths are handled.
- When screen turns **OFF**: enables sensor at 200mA LED current (`PS_CONF1=0xCA`, `PS_MS=0x0700`)
- Polls `PS_DATA` register via `smbus2` directly (no subprocess/sudo for I2C)
- When reading exceeds threshold (default: 50 counts): writes `bl_power=0` then `brightness=4095` via `sudo tee`, then calls `SimulateUserActivity` via D-Bus to reset the idle-blanker's timer
- When screen turns **ON**: disables sensor immediately (LED off)
- Logs `VCNL4200 init OK — device ID 0x1058 correct` at startup as a health check

Requires:
- `user` in the `i2c` group (for direct `/dev/i2c-0` access without sudo)
- One sudoers rule for the backlight write:
```
user ALL=(ALL) NOPASSWD: /usr/bin/tee /sys/class/backlight/backlight/brightness
user ALL=(ALL) NOPASSWD: /usr/bin/tee /sys/class/backlight/backlight/bl_power
```

> ⚠️ **KDE powerdevil display timeouts must be disabled.** See "Background" above. All timeouts are set to 0 in the shipped image.

### Tuning

All tuneable parameters are constants at the top of `/usr/local/bin/vcnl4200-proximity-daemon.py`. After editing, restart the service:
```bash
systemctl --user restart vcnl4200-proximity-daemon.service
```

| Constant | Default | Effect |
|----------|---------|--------|
| `PROXIMITY_THRESHOLD` | `50` | Counts above which an object is considered detected. Lower = triggers at greater distance but more false positives. Ambient noise is 3–11 counts. |
| `DEBOUNCE_COUNT` | `1` | How many consecutive readings above threshold before waking. Increase to 2–3 to reduce false triggers; each step adds one `POLLING_INTERVAL` of latency. |
| `WAKE_COOLDOWN` | `10.0` | Seconds the daemon waits before it can trigger another wake. Prevents rapid re-fires if the hand lingers. |
| `POLLING_INTERVAL` | `0.5` | Seconds between sensor reads (0.5 = 2 Hz). Lower = faster response but more I2C traffic. 0.2 is a practical minimum. |
| `PS_LED_200MA` | `0x0700` | LED current register value (reg 0x04). High byte bits[2:0] = LED_I: `000`=50mA `001`=75mA `010`=100mA `011`=120mA `100`=140mA `101`=160mA `110`=180mA `111`=200mA (current setting = 200mA). |
| `PS_CONF1_ENABLED` | `0xCA` | PS_CONF1 register: bits[7:6]=PS_IT (integration time, `11`=4T max), bits[5:4]=PS_DUTY (LED duty cycle, `00`=1/40 max), bits[3:2]=PS_PERS. 4T integration + 1/40 duty = maximum sensitivity. |

**Typical reading reference:**

| Condition | Typical reading |
|-----------|----------------|
| No object (ambient) | 3–11 counts |
| Hand at ~30 cm | 30–80 counts |
| Hand at ~15 cm | 80–300 counts |
| Hand at ~5 cm | 300–1044 counts |

> Note: exact values depend on hand size, skin reflectivity, and ambient IR. The readings above are with LED at 200mA and PS_IT=4T.

### Service management

```bash
# Status
systemctl --user status idle-blanker.service
systemctl --user status vcnl4200-proximity-daemon.service

# Live logs
journalctl --user -u idle-blanker.service -f
journalctl --user -u vcnl4200-proximity-daemon.service -f

# Restart after config change
systemctl --user restart idle-blanker.service
systemctl --user restart vcnl4200-proximity-daemon.service
```

### Register summary (VCNL4200)

| Register | Address | Purpose |
|----------|---------|--------|
| `ALS_CONF` | `0x00` | ALS enable/disable (bit 0 = ALS_SD) |
| `PS_CONF1_2` | `0x03` | PS config: duty cycle, integration time, PS_SD (bit 0) |
| `PS_CONF3_MS` | `0x04` | LED current (50 mA minimum = `0x00`) |
| `PS_DATA` | `0x08` | Proximity reading (16-bit, read-only) |
| `PROD_ID` | `0x0E` | Product ID = `0x1058` (low byte `0x58` = VCNL4200) |

---

## Building the Kernel Module

The TAS5782M driver must be built against the kernel tree with `CC=clang-18`. The kernel has KCFI enabled; a GCC-built module will fail silently (probe never called).

```bash
# In WSL with aarch64 cross-toolchain and clang-18 installed
make ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- CC=clang-18 \
     KBUILD_MODPOST_WARN=1 \
     -C /path/to/kernel-build-v619 \
     M=/path/to/tas5782m-src \
     modules
```

See [BUILDING.md](BUILDING.md) and [sources/scripts/build-wsl.sh](sources/scripts/build-wsl.sh).

---

## Partition Layout

```
eMMC
├── boot      (p18) — lk2nd secondary bootloader
├── system    (p24) — Ubuntu image (MBR + pmOS_boot + pmOS_root)
├── modem     (p1)  — Qualcomm modem firmware (read-only)
├── dsp       (p12) — ADSP firmware (read-only)
└── persist   (p27) — Qualcomm persist data
```

`userdata` does not exist as a separate partition on this device — the Android userdata area was merged into `system` (approximately 7 GB total).

---

## Repository Contents

```
FINAL/
├── README.md                   ← repository overview
├── DEPLOYMENT_STATE.md         ← image baseline vs post-install fixes tracker
├── AUDIO.md                    ← detailed audio setup and driver documentation
├── BUILDING.md                 ← module build workflow
├── FLASHING.md                 ← EDL + GPT + image flashing guide
├── LICENSE                     ← repository license split and upstream-license notes
├── prebuilt/
│   ├── lk2nd.img
│   └── snd-soc-tas5782m-dbg.ko
├── partitions/
│   └── LAYOUT.md               ← GPT and repartitioning notes
├── research/
│   └── camera/
│       ├── README.md
│       ├── analysis/
│       ├── inventories/
│       └── kernel_references/
└── sources/
    ├── driver/                 ← TAS5782M driver source + debug wrappers
    ├── scripts/                ← build and deploy helper scripts
    ├── patches/                ← kernel / DTS patches
    ├── vcnl4200-proximity-daemon.py
    └── vcnl4200-proximity-daemon.service
```

## Contributor Notes

If you want to help development or understand internals, start here:

- `research/wifi/analysis/mobile-wifi-kcm-link-fix-2026-06-05.md`
- `research/wifi/analysis/ath10k-gtk-rekey-fix-2026-06-05.md`
- `research/root/usb-network-access-2026-06-05.md`
- `research/camera/README.md`
- `research/sensors/README.md`
- `research/video/analysis/stock-video-decode-runtime-2026-05-28.md`
- `research/video/analysis/venus-decode-findings-2026-06-05.md`

---

## Credits

- **FelixKa** — original PMOS device package and TAS5782M driver reference implementation; established the register map, preboot sequence, and PDN polarity.
- **postmarketOS** project — `linux-postmarketos-qcom-msm8953` kernel tree used as the build base.
- Lenovo open-source release (`lenovo_cd-18781y.201218.2018_opensource`) for Android kernel sources.
