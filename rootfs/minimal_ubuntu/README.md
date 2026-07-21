# Minimal Ubuntu Baseline — ThinkSmart View CD-18781Y

**Image (baseline, internal only):** `miniubuntuplasma6.img`  
**Image (sanitized, for publish):** `../plasma6-ubuntu-qcom-msm8953.img`  
**Captured:** 2026-06-18  
**Size:** ~6.9 GB  

## What This Is

A stripped-down Ubuntu 24.04 LTS baseline for the Lenovo ThinkSmart View (APQ8053/MSM8953). All hardware-specific components are intact and verified working. This image is the starting point for the Plasma 6 + Plasma Mobile 6 upgrade.

Boot credentials: `ubuntu` / `thinksmart`  
Hostname: `thinksmarter`  
WiFi MAC: `50:5b:c2:74:5a:9b`  

## What Is Working

- Ubuntu 24.04 LTS userland
- Kernel `6.19.5-msm8953` with all custom drivers
- WiFi (ath10k_sdio, patched GTK rekey fix) — auto-connects on boot
- SSH (`openssh-server`, enabled and starts on boot)
- SDDM (display manager, running — no desktop session behind it at this stage)
- USB networking (`172.16.42.1`)
- All systemd services from the full image: `qup-i2c-pinctrl-fix`, `adsp-start`, `idle-blanker`, `vcnl4200-proximity-daemon`, `rotation-daemon`
- KDE neon PPA configured (`archive.neon.kde.org/user`, noble, arm64) — Plasma 6.7.0 available

## What Was Removed

### Desktop Environment
- Plasma Mobile 5.27 (`plasma-mobile`, `plasma-mobile-tweaks`, `plasma-nano`)
- Plasma Desktop 5.27 (`plasma-desktop`, `plasma-workspace`, `plasma-workspace-wayland`)
- KWin 5.27 (`kwin-wayland`, `kwin-x11`, `kwin-common`, `kwin-data`)
- All Plasma applets and addons (`plasma-discover`, `plasma-nm`, `plasma-pa`, `plasma-disks`, `plasma-firewall`, `plasma-integration`, `plasma-settings`, `plasma-systemmonitor`, `plasma-thunderbolt`, `plasma-vault`, `plasma-browser-integration`)
- KDE CLI tools, Plasma framework, xdg-desktop-portal-kde
- All KDE Frameworks 5 libraries (autoremoved)
- All Qt 5 QML modules and plugins (autoremoved)

### System Bloat
- `snapd` — package purged and leftover files under `/usr/lib/snapd` deleted
- `man-db`, `yelp` — documentation tools
- All non-English locale data (`/usr/share/locale/*` except `en`/`en_US`)
- Most Noto fonts — only kept `NotoSans-Regular`, `NotoSans-Bold`, `NotoSansMono-Regular`, `NotoSansSymbols*`
- `/usr/share/fonts/opentype` — entire directory removed
- `libpinyin` data (`/usr/lib/aarch64-linux-gnu/libpinyin`) — Chinese input not needed
- `webkit2gtk`, `yelp-xsl`, `docbook-*`, `groff-base` — documentation/browser stack autoremoved with Plasma

## Current State (as of 2026-06-17)

Plasma 6.7.0 from KDE neon is installed and verified working on the test device. This README describes the baseline image from which the upgrade was performed.

### What was required to complete the Plasma 6 install

1. **neon PPA pin fix** — `/etc/apt/preferences.d/99-neon.pref` (created by neon's own tools) was pinning all neon packages at priority 100, overriding the `neon-priority` file. Fixed by overwriting it with `Pin: origin archive.neon.kde.org` at priority 900. `Pin: release l=KDE neon - User Edition` does NOT work.

2. **KF5→KF6 file conflict resolution** — several Ubuntu KF5 packages owned files that neon's KF6 packages want to install. Required `dpkg --remove --force-depends` for `libkf5globalaccel-bin`, `kio-extras`, `libkf5xmlgui5`, then reinstall with `--force-overwrite`. Also `--force-overwrite` for `libkf5configcore5`, `libkf5solid5`, `libkf5notifications5`, `libkf5archive5` locale conflicts.

3. **snapd re-purge** — `ubuntu-minimal` pulled snapd back during dist-upgrade. Purged again.

4. **DPMS disabled** — Plasma 6 on a DSI display puts the panel into hardware sleep via DRM on DPMS. Panel cannot wake from input. Set `idleTime=0` in `[AC]` and `[Battery]` DPMSControl. Idle blanking delegated to idle-blanker.py (1800s).

See [FINAL/DEPLOYMENT_STATE.md](../../../FINAL/DEPLOYMENT_STATE.md) for full Plasma 6 upgrade steps.
