#!/usr/bin/env bash
set -euo pipefail

# Fix Plasma Mobile Wi-Fi quicksettings deep-link on the device.
# This script is intended to run ON DEVICE as a normal user with sudo access.

QML_FILE="/usr/share/plasma/quicksettings/org.kde.plasma.quicksetting.wifi/contents/ui/main.qml"
TS="$(date +%Y%m%d-%H%M%S)"
BK_DIR="/var/backups/mobile-wifi-kcm-link-${TS}"

if [[ ! -f "$QML_FILE" ]]; then
  echo "Missing quicksetting file: $QML_FILE" >&2
  exit 1
fi

echo "[1/4] Creating backup at $BK_DIR"
sudo mkdir -p "$BK_DIR"
sudo cp -a "$QML_FILE" "$BK_DIR/"

echo "[2/4] Patching settingsCommand"
# Force the command format expected by plasma-settings for direct module open.
sudo sed -i 's|settingsCommand: ".*"|settingsCommand: "plasma-settings -s -m kcm_mobile_wifi"|' "$QML_FILE"

echo "[3/4] Rebuilding sycoca"
sudo kbuildsycoca5

echo "[4/4] Restarting user plasmashell"
systemctl --user restart plasma-plasmashell.service 2>/dev/null || \
  systemctl --user restart plasmashell.service 2>/dev/null || true

echo "Done. Current line:"
grep -n 'settingsCommand' "$QML_FILE" || true
echo "Backup: $BK_DIR"
