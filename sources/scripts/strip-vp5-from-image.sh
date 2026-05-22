#!/bin/bash
# Strip VP5 and its bundled Qt libs from the Ubuntu rootfs image.

set -e

IMG="${1:-/mnt/c/pmos-temp/FINAL/rootfs/rootfs_audio_proximity_venus.img}"

if [ ! -f "$IMG" ]; then
    echo "ERROR: image not found: $IMG"
    exit 1
fi

echo "Image: $IMG"
echo "Setting up loop device..."
LODEV=$(sudo losetup -Pf --show "$IMG")
echo "Loop device: $LODEV"

MNT=$(mktemp -d)
sudo mount "${LODEV}p2" "$MNT"
echo "Mounted rootfs at $MNT"

echo ""
echo "=== Before: disk usage ==="
df -h "${LODEV}p2"

echo ""
echo "=== Removing VP5 ==="

sudo rm -fv \
    "$MNT/etc/systemd/system/multi-user.target.wants/vp5-daemon.service" \
    "$MNT/etc/systemd/system/multi-user.target.wants/vp5-wake.service" \
    "$MNT/etc/systemd/system/vp5-daemon.service" \
    "$MNT/etc/systemd/system/vp5-wake.service"

sudo rm -fv "$MNT/usr/local/bin/vp5-wake-daemon.py"
sudo rm -fv "$MNT/usr/local/bin/vp5-wake-daemon.py.bak-"*

sudo rm -rfv "$MNT/home/user/vp5"
sudo rm -rfv "$MNT/home/user/.config/DIVUS"
sudo rm -rfv "$MNT/home/user/.cache/DIVUS"
sudo rm -rfv "$MNT/home/user/Pictures/VP5"

echo ""
echo "=== After: disk usage ==="
df -h "${LODEV}p2"

echo ""
echo "=== Verifying no VP5 traces remain ==="
sudo find "$MNT/home/user" -iname "*vp5*" -o -iname "*divus*" 2>/dev/null | grep . && echo "WARNING: traces found above" || echo "Clean."
sudo find "$MNT/etc/systemd" -iname "*vp5*" 2>/dev/null | grep . && echo "WARNING: service traces found" || echo "Services clean."

echo ""
echo "Syncing and unmounting..."
sync
sudo umount "$MNT"
rmdir "$MNT"
sudo losetup -d "$LODEV"

echo ""
echo "Done. VP5 stripped from image."