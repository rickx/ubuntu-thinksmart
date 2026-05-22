#!/bin/bash
set -euo pipefail

SCRIPT_DIR=$(cd -- "$(dirname -- "$0")" && pwd)
REPO_ROOT=$(cd -- "$SCRIPT_DIR/../.." && pwd)

SRC_IMG=
OUT_IMG=
SSID=
PSK=
HOSTNAME=thinksmarter-bootstrap
LOGIN_USER=user
LOGIN_PASSWORD=
LAYOUT_FILE="$REPO_ROOT/partitions/ubuntu_layout.sfdisk"
APPLY_HELPER="$SCRIPT_DIR/apply-ubuntu-gpt.sh"

usage() {
    cat <<'EOF'
Usage:
  prepare-ubuntu-ssh-bootstrap-image.sh \
    --src /path/to/base-ubuntu.img \
    --out /path/to/output-bootstrap.img \
    --ssid "MyWiFi" \
    --psk "secret" \
    [--hostname thinksmarter-bootstrap] \
    [--login-user user] \
    [--login-password thinksmart]

This prepares an Ubuntu bootstrap image for the safer WiFi+SSH flow.
It assumes the image uses the Lenovo ThinkSmart View layout where p2 is the ext4 rootfs.
EOF
}

connection_filename() {
    printf '%s' "$1" | tr ' /\\:' '____'
}

service_exists() {
    local root_dir=$1
    local service_name=$2

    [ -f "$root_dir/etc/systemd/system/$service_name" ] || \
    [ -f "$root_dir/lib/systemd/system/$service_name" ] || \
    [ -f "$root_dir/usr/lib/systemd/system/$service_name" ]
}

enable_service() {
    local root_dir=$1
    local service_name=$2

    if service_exists "$root_dir" "$service_name"; then
        sudo systemctl --root="$root_dir" enable "$service_name" >/dev/null
    fi
}

set_shadow_password() {
    local root_dir=$1
    local user_name=$2
    local password_hash=$3

    if [ ! -f "$root_dir/etc/shadow" ]; then
        echo "WARNING: /etc/shadow not found, skipping password update for $user_name" >&2
        return
    fi

    local tmp_file
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

if [ -z "$SRC_IMG" ] || [ -z "$OUT_IMG" ] || [ -z "$SSID" ] || [ -z "$PSK" ]; then
    echo "ERROR: --src, --out, --ssid, and --psk are required" >&2
    usage >&2
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
    echo "ERROR: GPT helper not found: $APPLY_HELPER" >&2
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

CONNECTION_NAME=$(connection_filename "$SSID")
CONNECTION_PATH="$MOUNT_DIR/etc/NetworkManager/system-connections/$CONNECTION_NAME.nmconnection"

sudo install -d -m 755 "$MOUNT_DIR/usr/local/share/bootstrap"
sudo install -D -m 755 "$APPLY_HELPER" "$MOUNT_DIR/usr/local/sbin/apply-ubuntu-gpt.sh"
sudo install -D -m 644 "$LAYOUT_FILE" "$MOUNT_DIR/usr/local/share/bootstrap/ubuntu_layout.sfdisk"

cat <<EOF | sudo tee "$MOUNT_DIR/usr/local/share/bootstrap/BOOTSTRAP-NEXT-STEPS.txt" >/dev/null
Ubuntu SSH bootstrap image ready.

Expected flow:

    1. Run: python edl.py w system <your-bootstrap-image>.img
    2. Boot it and wait for WiFi association.
    3. SSH to $LOGIN_USER@$HOSTNAME (or the DHCP address).
    4. Run sudo /usr/local/sbin/apply-ubuntu-gpt.sh
    5. Re-enter EDL and run: python edl.py w system ubuntu-qcom-msm8953.img
EOF

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

if [ ! -f "$MOUNT_DIR/etc/NetworkManager/NetworkManager.conf" ]; then
    cat <<'EOF' | sudo tee "$MOUNT_DIR/etc/NetworkManager/NetworkManager.conf" >/dev/null
[main]
plugins=ifupdown,keyfile

[ifupdown]
managed=true

[device]
wifi.scan-rand-mac-address=yes
wifi.backend=wpa_supplicant
EOF
fi

echo "$HOSTNAME" | sudo tee "$MOUNT_DIR/etc/hostname" >/dev/null
if [ -f "$MOUNT_DIR/etc/hosts" ]; then
    sudo sed -i "s/127.0.1.1.*/127.0.1.1\t$HOSTNAME/" "$MOUNT_DIR/etc/hosts"
    sudo sed -i "s/lemalo/$HOSTNAME/g" "$MOUNT_DIR/etc/hosts"
    sudo sed -i "s/thinksmarter/$HOSTNAME/g" "$MOUNT_DIR/etc/hosts"
fi

if [ -f "$MOUNT_DIR/etc/ssh/sshd_config" ]; then
    sudo sed -i 's/^#\?PasswordAuthentication.*/PasswordAuthentication yes/' "$MOUNT_DIR/etc/ssh/sshd_config"
    sudo sed -i 's/^#\?PermitRootLogin.*/PermitRootLogin yes/' "$MOUNT_DIR/etc/ssh/sshd_config"
fi

enable_service "$MOUNT_DIR" NetworkManager.service
enable_service "$MOUNT_DIR" ssh.service

if [ -n "$LOGIN_PASSWORD" ]; then
    PASSWORD_HASH=$(openssl passwd -6 "$LOGIN_PASSWORD")
    set_shadow_password "$MOUNT_DIR" root "$PASSWORD_HASH"
    if grep -q "^$LOGIN_USER:" "$MOUNT_DIR/etc/passwd"; then
        set_shadow_password "$MOUNT_DIR" "$LOGIN_USER" "$PASSWORD_HASH"
    else
        echo "WARNING: user $LOGIN_USER not present in /etc/passwd, skipped user password update" >&2
    fi
fi

sync

echo "Prepared Ubuntu SSH bootstrap image: $OUT_IMG"
echo "Injected files:"
echo "  /usr/local/sbin/apply-ubuntu-gpt.sh"
echo "  /usr/local/share/bootstrap/ubuntu_layout.sfdisk"
echo "  /usr/local/share/bootstrap/BOOTSTRAP-NEXT-STEPS.txt"
echo "  /etc/NetworkManager/system-connections/$CONNECTION_NAME.nmconnection"
echo "Services enabled offline (when present):"
echo "  NetworkManager.service"
echo "  ssh.service"