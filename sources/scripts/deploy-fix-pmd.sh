#!/bin/bash
# Deploy the PRE_PMD keep-alive fix:
#   1. Build new module in WSL (must be run separately before this)
#   2. Install module on device
#   3. Reload driver
#   4. Install warmup service

set -e

DEVICE_USER=user
DEVICE_IP=192.168.0.85
MODULE_SRC=/home/fleverato/tas5782m-src/snd-soc-tas5782m-dbg.ko
MODULE_DEST=/lib/modules/6.19.5-msm8953/extra/snd-soc-tas5782m-dbg.ko

echo "=== Installing module ==="
cp /tmp/snd-soc-tas5782m-dbg.ko "$MODULE_DEST"
chmod 644 "$MODULE_DEST"
depmod -a

echo "=== Reloading driver ==="
# Gracefully: stop PipeWire, rmmod, insmod, restart PipeWire
systemctl --user -M ${DEVICE_USER}@ stop pipewire pipewire-pulse wireplumber 2>/dev/null || true
rmmod snd_soc_tas5782m_dbg 2>/dev/null || true
insmod "$MODULE_DEST"
systemctl --user -M ${DEVICE_USER}@ start wireplumber pipewire pipewire-pulse 2>/dev/null || true

echo "=== Installing warmup service ==="
install -m 755 /tmp/tas5782m-warmup.sh /usr/local/sbin/tas5782m-warmup.sh

cat > /etc/systemd/system/tas5782m-warmup.service << 'EOF'
[Unit]
Description=TAS5782M chip pre-initialization (absorbs 700ms first-play delay)
After=pipewire.service wireplumber.service sound.target
Wants=pipewire.service wireplumber.service

[Service]
Type=oneshot
User=user
Environment=XDG_RUNTIME_DIR=/run/user/1000
ExecStartPre=/bin/sleep 3
ExecStart=/usr/local/sbin/tas5782m-warmup.sh
RemainAfterExit=yes

[Install]
WantedBy=multi-user.target
EOF

systemctl daemon-reload
systemctl enable tas5782m-warmup.service

echo "=== Verifying module ==="
lsmod | grep tas
dmesg | grep tas5782m | tail -5

echo "DONE — run 'systemctl start tas5782m-warmup.service' to warm up now"
echo "Or reboot and the service will run automatically"
