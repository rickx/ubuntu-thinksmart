#!/bin/sh
set -eu

LAYOUT_FILE=${1:-/usr/local/share/bootstrap/ubuntu_layout.sfdisk}
TARGET_DEVICE=${2:-/dev/mmcblk0}

if [ ! -r "$LAYOUT_FILE" ]; then
    echo "ERROR: layout file not found: $LAYOUT_FILE" >&2
    exit 1
fi

if ! command -v sfdisk >/dev/null 2>&1; then
    echo "ERROR: sfdisk is not available in this image" >&2
    exit 1
fi

echo "Rewriting GPT on $TARGET_DEVICE using $LAYOUT_FILE"
echo "This changes the live partition table. Power off or reboot immediately after it completes."

sfdisk --force --no-reread "$TARGET_DEVICE" < "$LAYOUT_FILE"
sync

echo
echo "GPT rewrite complete."
echo "Next steps:"
echo "1. Power off or reboot the device."
echo "2. Put it back into EDL."
echo "3. Flash the full ubuntu-qcom-msm8953.img to the system partition."