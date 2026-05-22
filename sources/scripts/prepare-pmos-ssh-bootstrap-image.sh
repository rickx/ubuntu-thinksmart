#!/bin/bash
set -euo pipefail

SCRIPT_DIR=$(cd -- "$(dirname -- "$0")" && pwd)
REPO_ROOT=$(cd -- "$SCRIPT_DIR/../.." && pwd)

SRC_IMG=
OUT_IMG=
SSID=
PSK=
HOSTNAME=thinksmarter
LOGIN_USER=pmos
LOGIN_PASSWORD=
ENABLE_UNATTENDED_BOOTSTRAP=0
LAYOUT_FILE="$REPO_ROOT/partitions/ubuntu_layout.sfdisk"
APPLY_HELPER="$SCRIPT_DIR/apply-ubuntu-gpt.sh"
AUTO_HELPER="$SCRIPT_DIR/bootstrap-auto-apply-ubuntu-gpt.sh"
OPENRC_SERVICE="$SCRIPT_DIR/bootstrap-apply-ubuntu-gpt.openrc"

usage() {
    cat <<'EOF'
Usage:
  prepare-pmos-ssh-bootstrap-image.sh \
    --src /path/to/base-pmos.img \
    --out /path/to/output-bootstrap.img \
    --login-password thinksmart \
    [--ssid MyWiFi --psk secret] \
    [--hostname thinksmarter] \
    [--login-user pmos] \
    [--enable-unattended 0|1]

This prepares a pmOS-based SSH bootstrap image for the safer flashing flow.
It strips image-specific state from the source image, stages the GPT helper
payload, optionally injects a WiFi profile, and sets a known login password.
EOF
}

connection_filename() {
    printf '%s' "$1" | tr ' /\\:' '____'
}

set_shadow_password() {
    local root_dir=$1
    local user_name=$2
    local password_hash=$3
    local tmp_file

    if [ ! -f "$root_dir/etc/shadow" ]; then
        echo "WARNING: /etc/shadow not found, skipping password update for $user_name" >&2
        return
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

while [ $# -gt 0 ]; do
    case "$1" in
        --src)
            SRC_IMG=$2
            shift 2
            ;;
        --out)
            OUT_IMG=$2
            shift 2
            ;;
        --ssid)
            SSID=$2
            shift 2
            ;;
        --psk)
            PSK=$2
            shift 2
            ;;
        --hostname)
            HOSTNAME=$2
            shift 2
            ;;
        --login-user)
            LOGIN_USER=$2
            shift 2
            ;;
        --login-password)
            LOGIN_PASSWORD=$2
            shift 2
            ;;
        --enable-unattended)
            ENABLE_UNATTENDED_BOOTSTRAP=$2
            shift 2
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        *)
            echo "ERROR: unknown argument: $1" >&2
            usage >&2
            exit 1
            ;;
    esac
done

if [ -z "$SRC_IMG" ] || [ -z "$OUT_IMG" ] || [ -z "$LOGIN_PASSWORD" ]; then
    echo "ERROR: --src, --out, and --login-password are required" >&2
    usage >&2
    exit 1
fi

if { [ -n "$SSID" ] && [ -z "$PSK" ]; } || { [ -z "$SSID" ] && [ -n "$PSK" ]; }; then
    echo "ERROR: --ssid and --psk must be supplied together" >&2
    usage >&2
    exit 1
fi

if [ "$ENABLE_UNATTENDED_BOOTSTRAP" != "0" ] && [ "$ENABLE_UNATTENDED_BOOTSTRAP" != "1" ]; then
    echo "ERROR: --enable-unattended must be 0 or 1" >&2
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

if [ ! -f "$APPLY_HELPER" ] || [ ! -f "$AUTO_HELPER" ] || [ ! -f "$OPENRC_SERVICE" ]; then
    echo "ERROR: one or more helper files are missing from $SCRIPT_DIR" >&2
    exit 1
fi

if ! command -v openssl >/dev/null 2>&1; then
    echo "ERROR: openssl is required to generate password hashes" >&2
    exit 1
fi

mkdir -p "$(dirname -- "$OUT_IMG")"
cp "$SRC_IMG" "$OUT_IMG"

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

LOOP_DEV=$(sudo losetup --find --show -P "$OUT_IMG")
MOUNT_DIR=$(mktemp -d)
sudo mount "${LOOP_DEV}p2" "$MOUNT_DIR"

sudo install -D -m 755 "$APPLY_HELPER" "$MOUNT_DIR/usr/local/sbin/apply-ubuntu-gpt.sh"
sudo install -D -m 755 "$AUTO_HELPER" "$MOUNT_DIR/usr/local/sbin/bootstrap-auto-apply-ubuntu-gpt.sh"
sudo install -D -m 755 "$OPENRC_SERVICE" "$MOUNT_DIR/etc/init.d/bootstrap-apply-ubuntu-gpt"
sudo install -D -m 644 "$LAYOUT_FILE" "$MOUNT_DIR/usr/local/share/bootstrap/ubuntu_layout.sfdisk"

sudo rm -f "$MOUNT_DIR/etc/NetworkManager/system-connections/"*.nmconnection
sudo rm -f "$MOUNT_DIR/etc/ssh/ssh_host_"* "$MOUNT_DIR/etc/ssh/ssh_host_"*.pub

if [ -f "$MOUNT_DIR/etc/machine-id" ]; then
    : | sudo tee "$MOUNT_DIR/etc/machine-id" >/dev/null
fi

if [ -f "$MOUNT_DIR/var/lib/dbus/machine-id" ]; then
    : | sudo tee "$MOUNT_DIR/var/lib/dbus/machine-id" >/dev/null
fi

if [ -n "$SSID" ]; then
    CONNECTION_NAME=$(connection_filename "$SSID")
    CONNECTION_PATH="$MOUNT_DIR/etc/NetworkManager/system-connections/$CONNECTION_NAME.nmconnection"

    sudo install -d -m 700 "$MOUNT_DIR/etc/NetworkManager/system-connections"
    cat <<EOF | sudo tee "$CONNECTION_PATH" >/dev/null
[connection]
id=$SSID
uuid=$(uuidgen)
type=wifi
interface-name=wlan0
autoconnect=true
autoconnect-priority=100

[wifi]
mode=infrastructure
ssid=$SSID

[wifi-security]
auth-alg=open
key-mgmt=wpa-psk
psk=$PSK

[ipv4]
method=auto

[ipv6]
addr-gen-mode=default
method=auto

[proxy]
EOF
    sudo chmod 600 "$CONNECTION_PATH"
fi

if [ "$ENABLE_UNATTENDED_BOOTSTRAP" = "1" ]; then
    sudo install -d "$MOUNT_DIR/etc/runlevels/default"
    sudo ln -sf /etc/init.d/bootstrap-apply-ubuntu-gpt "$MOUNT_DIR/etc/runlevels/default/bootstrap-apply-ubuntu-gpt"
fi

if [ -f "$MOUNT_DIR/etc/init.d/sshd" ]; then
    sudo install -d "$MOUNT_DIR/etc/runlevels/default"
    sudo ln -sf /etc/init.d/sshd "$MOUNT_DIR/etc/runlevels/default/sshd"
fi

PASSWORD_HASH=$(openssl passwd -6 "$LOGIN_PASSWORD")
set_shadow_password "$MOUNT_DIR" root "$PASSWORD_HASH"
if grep -q "^$LOGIN_USER:" "$MOUNT_DIR/etc/passwd"; then
    set_shadow_password "$MOUNT_DIR" "$LOGIN_USER" "$PASSWORD_HASH"
else
    echo "WARNING: user $LOGIN_USER not present in /etc/passwd, skipped user password update" >&2
fi

cat <<EOF | sudo tee "$MOUNT_DIR/usr/local/share/bootstrap/BOOTSTRAP-NEXT-STEPS.txt" >/dev/null
pmOS SSH bootstrap image ready.

Expected flow:

    1. Run: python edl.py w system <your-bootstrap-image>.img
    2. If a WiFi profile was injected, wait for association and SSH to $LOGIN_USER@$HOSTNAME.
    3. If no WiFi profile was injected, prepare one before flashing or configure networking locally on first boot.
    4. Run sudo /usr/local/sbin/apply-ubuntu-gpt.sh
    5. Re-enter EDL and run: python edl.py w system ubuntu-qcom-msm8953.img

Default build safety:

    - unattended GPT rewrite is NOT enabled by default
    - existing WiFi profiles, SSH host keys, and machine-id were removed from the source image
EOF

echo "$HOSTNAME" | sudo tee "$MOUNT_DIR/etc/hostname" >/dev/null
if [ -f "$MOUNT_DIR/etc/hosts" ]; then
    sudo sed -i "s/127.0.1.1.*/127.0.1.1\t$HOSTNAME/" "$MOUNT_DIR/etc/hosts"
    sudo sed -i "s/felix-cd18781y/$HOSTNAME/g" "$MOUNT_DIR/etc/hosts"
    sudo sed -i "s/bootstrap-cd18781y/$HOSTNAME/g" "$MOUNT_DIR/etc/hosts"
    sudo sed -i "s/lemalo/$HOSTNAME/g" "$MOUNT_DIR/etc/hosts"
    sudo sed -i "s/thinksmarter/$HOSTNAME/g" "$MOUNT_DIR/etc/hosts"
fi

sync

echo "Prepared pmOS SSH bootstrap image: $OUT_IMG"
echo "Sanitized from source image:"
echo "  existing WiFi profiles removed"
echo "  existing SSH host keys removed"
echo "  machine-id cleared"
if [ -n "$SSID" ]; then
    echo "Injected WiFi profile for SSID: $SSID"
else
    echo "Injected WiFi profile: none"
fi
echo "Login user: $LOGIN_USER"
echo "Hostname: $HOSTNAME"
if [ "$ENABLE_UNATTENDED_BOOTSTRAP" = "1" ]; then
    echo "Unattended boot-time GPT rewrite: ENABLED (experimental)"
else
    echo "Unattended boot-time GPT rewrite: DISABLED (safe default)"
fi