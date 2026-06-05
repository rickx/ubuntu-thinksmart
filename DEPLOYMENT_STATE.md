# Deployment State Tracker

This file tracks what is already in the published base image versus what must be applied after first boot.

## Public Image Baseline

Current published image reference:

- `ubuntu-qcom-msm8953.img` (see README links)

## Included In Image (Confirmed)

- No post-install UI/driver fixes are currently marked as confirmed-in-image.
- Treat all operational fixes below as post-install unless explicitly promoted after clean-flash verification.

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
