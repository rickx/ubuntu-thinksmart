# Mobile Wi-Fi KCM Link Fix (2026-06-05)

## Summary

Plasma Mobile quicksettings Wi-Fi deep-linking now works on this image by using the mobile settings frontend with explicit module flags:

`plasma-settings -s -m kcm_mobile_wifi`

## Why previous commands failed

- `plasma-open-settings kcm_mobile_wifi`
  - launched `plasma-settings` but did not reliably deep-link to the Wi-Fi module.

- `/usr/bin/kcmshell5 kcm_mobile_wifi`
  - failed at runtime on this image: `Could not find module 'kcm_mobile_wifi'`.

- `systemsettings kcm_mobile_wifi`
  - opened the wrong settings frontend/view hierarchy (desktop-like icon view).

- `plasma-settings kcm_mobile_wifi`
  - positional argument form falls back to list; this app expects `-m` for module selection.

## Confirmed command semantics

`plasma-settings --help` shows:

- `-m, --module <modulename>`: module to open
- `-s, --singleModule`: show only one module (requires `--module`)

## Applied runtime configuration

Quicksettings Wi-Fi tile now uses:

`settingsCommand: "plasma-settings -s -m kcm_mobile_wifi"`

## Related fix during same session

Wi-Fi KCM empty-page regression was fixed by removing an invalid QML property handler in `ConnectionItemDelegate.qml` and using safe reactive logic.

## Operational note

Use `kbuildsycoca5` and restart user `plasmashell` after edits; reboot is not required.

## Access path used

The debugging and deployment flow in this session used USB networking (not Wi-Fi-only SSH):

- `user@172.16.42.1`

This should be treated as the primary safe management path while iterating on network UI behavior.
