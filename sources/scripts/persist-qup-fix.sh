#!/bin/sh
# Deploy persistent fix for 78b5000.i2c pinctrl regression:
# GPIO2/3 end up in GPIO mode (func0) after boot; unbind+rebind restores
# blsp_i2c1 mux. power/control=on prevents future regression.

QUP_SCRIPT=/usr/local/sbin/qup-i2c-pinctrl-fix.sh
SERVICE=/etc/systemd/system/qup-i2c-pinctrl-fix.service

# 1. Install the fix script
cat > "$QUP_SCRIPT" << 'EOF'
#!/bin/sh
# Fix 78b5000.i2c pinctrl state: GPIO2/3 lose blsp_i2c1 mux after boot.
# Unbind/rebind restores it; power/control=on prevents regression.
QUP_PATH=/sys/bus/platform/devices/78b5000.i2c
DRIVER_PATH=/sys/bus/platform/drivers/i2c_qup
DEV=78b5000.i2c

# Unbind
echo "$DEV" > "$DRIVER_PATH/unbind" 2>/dev/null
sleep 0.3

# Rebind (triggers pinctrl_bind_pins → sets GPIO2/3 to blsp_i2c1)
echo "$DEV" > "$DRIVER_PATH/bind" 2>/dev/null
sleep 0.5

# Prevent PM runtime autosuspend (keeps GPIO2/3 in blsp_i2c1 mode)
echo on > "$QUP_PATH/power/control" 2>/dev/null
logger -t qup-i2c-fix "78b5000.i2c pinctrl restored, PM autosuspend disabled"
EOF
chmod 755 "$QUP_SCRIPT"

# 2. Install the systemd service
cat > "$SERVICE" << 'EOF'
[Unit]
Description=Restore 78b5000.i2c pinctrl state (blsp_i2c1)
After=sysinit.target
Before=sound.target alsa-restore.service

[Service]
Type=oneshot
ExecStart=/usr/local/sbin/qup-i2c-pinctrl-fix.sh
RemainAfterExit=yes

[Install]
WantedBy=multi-user.target
EOF

# 3. Enable the service
systemctl daemon-reload
systemctl enable qup-i2c-pinctrl-fix.service
systemctl status qup-i2c-pinctrl-fix.service --no-pager

echo ""
echo "=== Fix deployed. Will run automatically on next boot. ==="
echo "=== To test now without reboot: sudo systemctl start qup-i2c-pinctrl-fix.service ==="
