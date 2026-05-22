#!/bin/bash
# Rename vp5-wake-daemon → screen-wake-daemon on the live device.

set -e

NEW_BIN=/usr/local/bin/screen-wake-daemon.py
NEW_SVC=screen-wake.service

cp /tmp/screen-wake-daemon.py "$NEW_BIN"
chmod 755 "$NEW_BIN"

cat > /etc/systemd/system/$NEW_SVC << 'EOF'
[Unit]
Description=Screen Wake Daemon (touch + accelerometer)
After=multi-user.target

[Service]
Type=simple
ExecStart=/usr/bin/python3 /usr/local/bin/screen-wake-daemon.py
Restart=always
RestartSec=1
StandardOutput=journal
StandardError=journal

[Install]
WantedBy=multi-user.target
EOF

systemctl daemon-reload

systemctl stop vp5-wake.service 2>/dev/null || true
systemctl disable vp5-wake.service 2>/dev/null || true

rm -f /etc/systemd/system/vp5-wake.service
rm -f /etc/systemd/system/multi-user.target.wants/vp5-wake.service
rm -f /usr/local/bin/vp5-wake-daemon.py
rm -f /usr/local/bin/vp5-wake-daemon.py.bak-*

systemctl enable $NEW_SVC
systemctl start $NEW_SVC

echo "Status:"
systemctl status $NEW_SVC --no-pager