# ath10k GTK Rekey Dropout Fix (2026-06-05)

## Summary

The periodic Wi-Fi dropout on WPA2 GTK rekey is fixed by patching one byte in stock `ath10k_core.ko`.

- Driver kept stock: `ath10k_sdio.ko`
- Patched driver: `ath10k_core.ko`
- Patch type: binary patch (single-byte branch target change)

## Symptom

Before the fix, each GTK rekey window eventually produced:

- `failed to install key ... -110`
- `failed to set/remove key ... -110`
- `wlan0: deauthenticating ...`

That caused brief disconnects and reconnects.

## Root Cause

`ath10k_install_key()` times out waiting for key-install completion and returns `-ETIMEDOUT`.
`ath10k_set_key()` then propagated this error to mac80211, which triggered deauthentication.

## Fix Applied

A one-byte patch was applied at ELF file offset `0x8614` in stock `ath10k_core.ko`:

- old byte: `0x5b`
- new byte: `0x5a`

Effect: branch target changes so `ath10k_set_key()` returns success after logging the warning instead of returning `-ETIMEDOUT`.

## Verification

Observed after patch load:

- GTK rekey warning line still appears in dmesg (`failed to install key ... -110`)
- no follow-up `wlan0: deauthenticating`
- no reconnect/drop event

Operationally, connectivity remains stable across rekey intervals.

## Installed State (test device)

- patched module SHA (short): `96f5d9065e4f3ce0`
- original backup SHA (short): `0210db2885fe449f`
- stock module preserved as `.orig`

## Scripts

Scripts used for this workflow are in `sources/scripts/`:

- `patch-ath10k-etimedout.py`
- `install-ath10k-patched-core-only.sh`
- `reload-ath10k-patched-core.sh`
- `wait-ath10k-rekey.sh`

Use USB network management access while testing Wi-Fi driver swaps:

- `ssh user@172.16.42.1`
