#!/bin/bash
set -euo pipefail

SCRIPT_DIR=$(cd -- "$(dirname -- "$0")" && pwd)
REPO_ROOT=$(cd -- "$SCRIPT_DIR/../.." && pwd)

SRC_IMG=${1:-/mnt/c/pmos-temp/qcom-msm8953.img}
OUT_IMG=${2:-$REPO_ROOT/rootfs/bootstrap-qcom-msm8953.img}
ENABLE_UNATTENDED_BOOTSTRAP=${ENABLE_UNATTENDED_BOOTSTRAP:-0}
LAYOUT_FILE="$REPO_ROOT/partitions/ubuntu_layout.sfdisk"
APPLY_HELPER="$SCRIPT_DIR/apply-ubuntu-gpt.sh"
AUTO_HELPER="$SCRIPT_DIR/bootstrap-auto-apply-ubuntu-gpt.sh"
OPENRC_SERVICE="$SCRIPT_DIR/bootstrap-apply-ubuntu-gpt.openrc"

if [ "$ENABLE_UNATTENDED_BOOTSTRAP" != "0" ] && [ "$ENABLE_UNATTENDED_BOOTSTRAP" != "1" ]; then
    echo "ERROR: ENABLE_UNATTENDED_BOOTSTRAP must be 0 or 1" >&2
    exit 1
fi

if [ ! -f "$SRC_IMG" ]; then
    echo "ERROR: source image not found: $SRC_IMG" >&2
    exit 1
fi

if [ ! -f "$LAYOUT_FILE" ]; then
    echo "ERROR: layout file not found: $LAYOUT_FILE" >&2
    exit 1
fi

if [ ! -f "$APPLY_HELPER" ]; then
    echo "ERROR: helper script not found: $APPLY_HELPER" >&2
    exit 1
fi

if [ ! -f "$AUTO_HELPER" ]; then
    echo "ERROR: helper script not found: $AUTO_HELPER" >&2
    exit 1
fi

if [ ! -f "$OPENRC_SERVICE" ]; then
    echo "ERROR: OpenRC service not found: $OPENRC_SERVICE" >&2
    exit 1
fi

mkdir -p "$(dirname -- "$OUT_IMG")"
cp "$SRC_IMG" "$OUT_IMG"

LOOP_DEV=""
MOUNT_DIR=""

cleanup() {
    set +e
    if [ -n "$MOUNT_DIR" ] && mountpoint -q "$MOUNT_DIR"; then
        sudo umount "$MOUNT_DIR"
    fi
    if [ -n "$MOUNT_DIR" ] && [ -d "$MOUNT_DIR" ]; then
        rmdir "$MOUNT_DIR"
    fi
    if [ -n "$LOOP_DEV" ]; then
        sudo losetup -d "$LOOP_DEV"
    fi
}
trap cleanup EXIT

LOOP_DEV=$(sudo losetup --find --show -P "$OUT_IMG")
MOUNT_DIR=$(mktemp -d)
sudo mount "${LOOP_DEV}p2" "$MOUNT_DIR"

sudo install -D -m 755 "$APPLY_HELPER" "$MOUNT_DIR/usr/local/sbin/apply-ubuntu-gpt.sh"
sudo install -D -m 755 "$AUTO_HELPER" "$MOUNT_DIR/usr/local/sbin/bootstrap-auto-apply-ubuntu-gpt.sh"
sudo install -D -m 755 "$OPENRC_SERVICE" "$MOUNT_DIR/etc/init.d/bootstrap-apply-ubuntu-gpt"
sudo install -D -m 644 "$LAYOUT_FILE" "$MOUNT_DIR/usr/local/share/bootstrap/ubuntu_layout.sfdisk"

if [ "$ENABLE_UNATTENDED_BOOTSTRAP" = "1" ]; then
    sudo install -d "$MOUNT_DIR/etc/runlevels/default"
    sudo ln -sf /etc/init.d/bootstrap-apply-ubuntu-gpt "$MOUNT_DIR/etc/runlevels/default/bootstrap-apply-ubuntu-gpt"
fi

cat <<'EOF' | sudo tee "$MOUNT_DIR/usr/local/share/bootstrap/BOOTSTRAP-NEXT-STEPS.txt" >/dev/null
Bootstrap image ready.

This image contains the Ubuntu GPT rewrite helpers.

Default build safety:

    - unattended GPT rewrite is NOT enabled by default
    - this avoids an untested automatic partition rewrite on first boot

If you intentionally build with ENABLE_UNATTENDED_BOOTSTRAP=1, the image will:

    1. Boot.
    2. Apply ubuntu_layout.sfdisk automatically.
    3. Power off.

What to expect:

    1. Boot the image.
    2. Only proceed with unattended mode after one real-device validation.
    3. Re-enter EDL after the GPT rewrite step completes.
    4. Run: python edl.py w system ubuntu-qcom-msm8953.img

Do not publish the unattended mode as the default flow until it has been tested once on hardware.
EOF

echo "thinksmarter" | sudo tee "$MOUNT_DIR/etc/hostname" >/dev/null

if [ -f "$MOUNT_DIR/etc/hosts" ]; then
        sudo sed -i 's/lemalo/thinksmarter/g' "$MOUNT_DIR/etc/hosts"
        sudo sed -i 's/bootstrap-cd18781y/thinksmarter/g' "$MOUNT_DIR/etc/hosts"
fi

sync

echo "Prepared bootstrap image: $OUT_IMG"
if [ "$ENABLE_UNATTENDED_BOOTSTRAP" = "1" ]; then
    echo "Unattended boot-time GPT rewrite: ENABLED (experimental)"
else
    echo "Unattended boot-time GPT rewrite: DISABLED (safe default)"
fi
echo "Injected files:"
echo "  /usr/local/sbin/apply-ubuntu-gpt.sh"
echo "  /usr/local/sbin/bootstrap-auto-apply-ubuntu-gpt.sh"
echo "  /etc/init.d/bootstrap-apply-ubuntu-gpt"
if [ "$ENABLE_UNATTENDED_BOOTSTRAP" = "1" ]; then
    echo "  /etc/runlevels/default/bootstrap-apply-ubuntu-gpt"
fi
echo "  /usr/local/share/bootstrap/ubuntu_layout.sfdisk"
echo "  /usr/local/share/bootstrap/BOOTSTRAP-NEXT-STEPS.txt"