# Deployment State Tracker

This file tracks what is already in the published base image versus what must be applied after first boot.

## Published Images

| File | Desktop | State | Notes |
|------|---------|-------|-------|
| `plasma6-ubuntu-qcom-msm8953.img` | Plasma Mobile 6.7.0 (KDE neon) | Patched, upload to MEGA pending | Updated 2026-07-21: vkboard fix + daemon updates (idle-blanker, screen-wake, vcnl4200) |

The image uses kernel `6.19.5-msm8953`. It has SSH host keys present and a unique machine-id UUID — SSH and networking work immediately on first boot. WiFi injection works with `prepare-ubuntu-ssh-bootstrap-image.sh`.

### Plasma 6 image — what is baked in

- Plasma 6.7.0 from KDE neon, fully installed
- neon PPA pinned correctly (`Pin: origin archive.neon.kde.org`, priority 900)
- DPMS disabled (`DimDisplayIdleTimeoutSec=0`, `TurnOffDisplayIdleTimeoutSec=0` in `~/.config/powerdevilrc`) — required for Plasma 6 on DSI; `powermanagementprofilesrc` is the Plasma 5 legacy format and is ignored by Plasma 6
- idle-blanker.py: 1800s timeout, `BLANK_BRIGHTNESS=10` + SimulateUserActivity D-Bus calls to prevent KWin-induced DRM Off (2026-07-21 update)
- screen-wake-daemon.py: NEW (2026-07-21) — Touch + accelerometer wake handler on DSI blank; restores backlight and DRM connector state
- screen-wake.service: Enabled to auto-start screen-wake-daemon
- vcnl4200-proximity-daemon.py: Updated with latest proximity wake logic (2026-07-21)
- ath10k GTK rekey patch
- qup-i2c-pinctrl-fix.service
- SDDM autologin to `plasma-mobile.desktop`
- Virtual keyboard (Maliit) fix: Qt5 maliit launched with `QT_QPA_PLATFORM=wayland` in KWin 6 session (2026-07-21)
  - Custom `.desktop` file at `~/.local/share/applications/maliit-keyboard-wl.desktop`
  - KWin configured to use it via `~/.config/kwinrc [Wayland] InputMethod`
  - Keyboard pops up on text-field focus (no manual invocation needed)
- No snapd
- WiFi credentials cleared, shell history cleared, logs cleared, Claude artifacts removed
- SSH host keys present (RSA/ECDSA/Ed25519) — SSH starts immediately on first boot
- machine-id set to UUID `e4929f7a26e5465b9862f66dcea87041` — networking works on first boot

## Included In Image (Confirmed)

All operational fixes are baked into the published image as of 2026-07-21 (vkboard fix + daemon updates).

## Post-Install Fixes (Current)

- Plasma Mobile Wi-Fi settings deep-link fix (`plasma-settings -s -m kcm_mobile_wifi`)
- Wi-Fi page active IP display
- Brightness helper policy/binary fix (`sources/scripts/setup-backlighthelper.sh`)
- ath10k GTK rekey dropout fix (patched `ath10k_core.ko`)
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
