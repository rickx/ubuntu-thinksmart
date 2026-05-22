#!/bin/bash
set -euo pipefail

SCRIPT_DIR=$(cd -- "$(dirname -- "$0")" && pwd)
REPO_ROOT=$(cd -- "$SCRIPT_DIR/../.." && pwd)

IMG=${1:-$REPO_ROOT/rootfs/ubuntu-qcom-msm8953.img}
LOGIN_USER=${LOGIN_USER:-ubuntu}
LOGIN_PASSWORD=${LOGIN_PASSWORD:-thinksmart}
TARGET_HOSTNAME=${TARGET_HOSTNAME:-thinksmarter}

if [ ! -f "$IMG" ]; then
    echo "ERROR: image not found: $IMG" >&2
    exit 1
fi

if ! command -v openssl >/dev/null 2>&1; then
    echo "ERROR: openssl is required" >&2
    exit 1
fi

if ! command -v usermod >/dev/null 2>&1 || ! command -v groupmod >/dev/null 2>&1; then
    echo "ERROR: usermod and groupmod are required" >&2
    exit 1
fi

LOOP_DEV=
MOUNT_DIR=

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

set_shadow_password() {
    local root_dir=$1
    local user_name=$2
    local password_hash=$3
    local tmp_file

    if [ ! -f "$root_dir/etc/shadow" ]; then
        echo "ERROR: /etc/shadow not found in mounted image" >&2
        exit 1
    fi

    tmp_file=$(mktemp)

    sudo awk -F: -v user_name="$user_name" -v password_hash="$password_hash" '
        BEGIN { OFS = ":" }
        $1 == user_name { $2 = password_hash }
        { print }
    ' "$root_dir/etc/shadow" > "$tmp_file"

    sudo install -m 600 "$tmp_file" "$root_dir/etc/shadow"
    rm -f "$tmp_file"
}

replace_in_tree() {
    local root_dir=$1
    local from=$2
    local to=$3
    shift 3

    while [ $# -gt 0 ]; do
        local search_dir=$1
        shift

        if [ -d "$root_dir$search_dir" ]; then
            while IFS= read -r -d '' file_path; do
                sudo sed -i "s|$from|$to|g" "$file_path"
            done < <(sudo grep -R -l -Z -- "$from" "$root_dir$search_dir" 2>/dev/null || true)
        fi
    done
}

PASSWORD_HASH=$(openssl passwd -6 "$LOGIN_PASSWORD")

echo "Image: $IMG"
LOOP_DEV=$(sudo losetup --find --show -P "$IMG")
MOUNT_DIR=$(mktemp -d)
sudo mount "${LOOP_DEV}p2" "$MOUNT_DIR"

if grep -q '^user:' "$MOUNT_DIR/etc/passwd" && ! grep -q "^$LOGIN_USER:" "$MOUNT_DIR/etc/passwd"; then
    if grep -q '^user:' "$MOUNT_DIR/etc/group"; then
        sudo groupmod --root "$MOUNT_DIR" --new-name "$LOGIN_USER" user
    fi

    sudo usermod --root "$MOUNT_DIR" --login "$LOGIN_USER" --home "/home/$LOGIN_USER" --move-home user

    replace_in_tree "$MOUNT_DIR" '/home/user' "/home/$LOGIN_USER" /etc /usr/share
    replace_in_tree "$MOUNT_DIR" 'User=user' "User=$LOGIN_USER" /etc
    replace_in_tree "$MOUNT_DIR" 'autologin-user=user' "autologin-user=$LOGIN_USER" /etc

    if [ -f "$MOUNT_DIR/etc/sddm.conf.d/99-vp5-autologin.conf" ]; then
        sudo mv "$MOUNT_DIR/etc/sddm.conf.d/99-vp5-autologin.conf" "$MOUNT_DIR/etc/sddm.conf.d/99-thinksmart-autologin.conf"
    fi
fi

if ! grep -q "^$LOGIN_USER:" "$MOUNT_DIR/etc/passwd"; then
    echo "ERROR: login user $LOGIN_USER not present after rename/update" >&2
    exit 1
fi

set_shadow_password "$MOUNT_DIR" root "$PASSWORD_HASH"
set_shadow_password "$MOUNT_DIR" "$LOGIN_USER" "$PASSWORD_HASH"

if [ -d "$MOUNT_DIR/etc/NetworkManager/system-connections" ]; then
    sudo find "$MOUNT_DIR/etc/NetworkManager/system-connections" -maxdepth 1 -type f -name '*.nmconnection' -delete
fi

if [ -d "$MOUNT_DIR/etc/ssh" ]; then
    sudo find "$MOUNT_DIR/etc/ssh" -maxdepth 1 -type f -name 'ssh_host_*' -delete
    sudo install -d -m 755 "$MOUNT_DIR/etc/ssh/sshd_config.d"
    cat <<'EOF' | sudo tee "$MOUNT_DIR/etc/ssh/sshd_config.d/99-thinksmart-release.conf" >/dev/null
PasswordAuthentication yes
PermitRootLogin yes
EOF
fi

if [ -d "$MOUNT_DIR/home/$LOGIN_USER/.ssh" ]; then
    sudo rm -rf "$MOUNT_DIR/home/$LOGIN_USER/.ssh"
fi

if [ -d "$MOUNT_DIR/root/.ssh" ]; then
    sudo rm -rf "$MOUNT_DIR/root/.ssh"
fi

if [ -f "$MOUNT_DIR/etc/machine-id" ]; then
    : | sudo tee "$MOUNT_DIR/etc/machine-id" >/dev/null
fi

if [ -f "$MOUNT_DIR/var/lib/dbus/machine-id" ]; then
    : | sudo tee "$MOUNT_DIR/var/lib/dbus/machine-id" >/dev/null
fi

sudo rm -f "$MOUNT_DIR/var/lib/systemd/random-seed"
sudo rm -f "$MOUNT_DIR/home/$LOGIN_USER/.bash_history"
sudo rm -f "$MOUNT_DIR/root/.bash_history"

echo "$TARGET_HOSTNAME" | sudo tee "$MOUNT_DIR/etc/hostname" >/dev/null
if [ -f "$MOUNT_DIR/etc/hosts" ]; then
    sudo sed -i "s/127.0.1.1.*/127.0.1.1\t$TARGET_HOSTNAME/" "$MOUNT_DIR/etc/hosts"
    sudo sed -i "s/lemalo/$TARGET_HOSTNAME/g" "$MOUNT_DIR/etc/hosts"
    sudo sed -i "s/thinksmarter/$TARGET_HOSTNAME/g" "$MOUNT_DIR/etc/hosts"
    sudo sed -i "s/PCFLFIXPC/$TARGET_HOSTNAME/g" "$MOUNT_DIR/etc/hosts"
fi

sync

echo "Sanitized Ubuntu publish image: $IMG"
echo "Login user: $LOGIN_USER"
echo "Hostname: $TARGET_HOSTNAME"
echo "PasswordAuthentication: yes"
echo "PermitRootLogin: yes"
echo "Cleared state: WiFi profiles, SSH host keys, machine-id, random seed, shell history"