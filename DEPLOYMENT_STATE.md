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

## Release Guidance

When post-install steps become stable and low-risk, roll them into the next image release and update this file:

1. Move the item from Post-Install Fixes to Included In Image
2. Record the image tag/version where it became integrated
3. Keep scripts available for rollback/recovery for at least one release cycle
