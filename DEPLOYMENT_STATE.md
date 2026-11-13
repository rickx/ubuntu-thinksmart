# Deployment State Tracker

This file tracks what is already in the published base image versus what must be applied after first boot.

## Published Images

| File | Desktop | State | Notes |
|------|---------|-------|-------|
| `plasma6-ubuntu-qcom-msm8953-20261113.img` | Plasma Mobile 6.7.0 (KDE neon) | Published on MEGA (2026-11-13) | 2026-11-13: front camera (PR #6), rotation-daemon fix, first-boot SSH host keys / machine-id; replaces the 2026-07-21 upload, whose boot partition was missing the kernel, initramfs and DTB |

SHA-256 of the 2026-11-13 image: `141874888c83f2b739e617dc3306e2bfe0dc2df4462388b0df91c0faa643aac5`

The image uses kernel `6.19.5-msm8953`. SSH host keys and `machine-id` are generated on first boot, so SSH and networking work immediately and every device gets its own identity. WiFi injection works with `prepare-ubuntu-ssh-bootstrap-image.sh`.

### Plasma 6 image — what is baked in

- Plasma 6.7.0 from KDE neon, fully installed
- neon PPA pinned correctly (`Pin: origin archive.neon.kde.org`, priority 900)
- DPMS disabled (`DimDisplayIdleTimeoutSec=0`, `TurnOffDisplayIdleTimeoutSec=0` in `~/.config/powerdevilrc`) — required for Plasma 6 on DSI; `powermanagementprofilesrc` is the Plasma 5 legacy format and is ignored by Plasma 6
- idle-blanker.py: 1800s timeout, `BLANK_BRIGHTNESS=10` + SimulateUserActivity D-Bus calls to prevent KWin-induced DRM Off (2026-07-21 update)
- screen-wake-daemon.py: NEW (2026-07-21) — Touch + accelerometer wake handler on DSI blank; restores backlight and DRM connector state
- screen-wake.service: Enabled to auto-start screen-wake-daemon
- vcnl4200-proximity-daemon.py: Updated with latest proximity wake logic (2026-07-21)
- ath10k GTK rekey patch (patched `ath10k_core.ko`, original kept as `ath10k_core.ko.orig`)
- Front camera (2026-11-13): `thinksmart-camera` 0.2 package — S5KC505A driver (clang-18/KCFI) loaded via `modules-load.d`, libcamera 0.8 + PR #6 patches in `/opt/libcamera` (GPU SoftISP), PipeWire libcamera plugin, GStreamer `libcamerasrc`, DMA-heap udev rule, WirePlumber rule hiding the raw CAMSS nodes — plus the camera DTB in `/boot`. See `sources/camera/ubuntu/README.md`
- rotation-daemon waits for KWin before the first `kscreen-doctor` call (no more core dump at every login) (2026-11-13)
- qup-i2c-pinctrl-fix.service
- SDDM autologin to `plasma-mobile.desktop`
- Virtual keyboard (Maliit) fix: Qt5 maliit launched with `QT_QPA_PLATFORM=wayland` in KWin 6 session (2026-07-21)
  - Custom `.desktop` file at `~/.local/share/applications/maliit-keyboard-wl.desktop`
  - KWin configured to use it via `~/.config/kwinrc [Wayland] InputMethod`
  - Keyboard pops up on text-field focus (no manual invocation needed)
- No snapd
- WiFi credentials cleared, shell history cleared, logs cleared, Claude artifacts removed
- SSH host keys generated on first boot by `thinksmart-ssh-hostkeys.service` (`ssh-keygen -A`, ordered before `ssh.service`)
- `machine-id` empty in the image, generated on first boot
- Login `ubuntu` / `thinksmart`, hostname `thinksmarter`

## Included In Image (Confirmed)

All operational fixes listed above are baked into the published image as of 2026-11-13 (front camera, rotation-daemon fix; earlier: vkboard fix + daemon updates of 2026-07-21).

## Post-Install Fixes (Current)

- Plasma Mobile Wi-Fi settings deep-link fix (`plasma-settings -s -m kcm_mobile_wifi`)
- Wi-Fi page active IP display
- Brightness helper policy/binary fix (`sources/scripts/setup-backlighthelper.sh`)
- ath10k GTK rekey dropout fix: baked into the 2026-11-13 image; scripts kept for custom installs and rollback
  - scripts in `sources/scripts/`:
    - `patch-ath10k-etimedout.py`
    - `install-ath10k-patched-core-only.sh`
    - `reload-ath10k-patched-core.sh`
    - `wait-ath10k-rekey.sh`



## Verification Checklist

After applying post-install fixes, verify:

- Wi-Fi remains connected through at least one GTK rekey interval
- dmesg can still show key warning lines, but no `wlan0: deauthenticating`
- normal browsing/SSH stays active across rekey windows

## How Fixes Move Into The Public Image

Some fixes are published first as post-install steps so users can test them safely before they are baked into a new image.

A fix is moved from **Post-Install Fixes** to **Included In Image (Confirmed)** only after:

1. It stays stable in normal use (no regressions reported during the validation window).
2. It is verified on a clean-flash device with the same public image flow used by users.
3. The image version that includes it is documented here.

After integration, helper scripts are still kept for at least one release cycle so users can recover or roll back if needed.
