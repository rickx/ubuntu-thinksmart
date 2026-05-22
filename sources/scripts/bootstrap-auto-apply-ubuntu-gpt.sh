#!/bin/sh
set -eu

RESULT_FILE=/root/bootstrap-apply-ubuntu-gpt.log

log() {
    printf '%s\n' "$*"
    printf '%s\n' "$*" >> "$RESULT_FILE"
    printf '%s\n' "$*" > /dev/tty0 2>/dev/null || true
}

power_off() {
    sync
    poweroff -f 2>/dev/null || poweroff 2>/dev/null || halt -p 2>/dev/null || reboot -f
}

: > "$RESULT_FILE"

log "[bootstrap] Starting automatic Ubuntu GPT rewrite"
log "[bootstrap] Using /usr/local/share/bootstrap/ubuntu_layout.sfdisk on /dev/mmcblk0"

if /usr/local/sbin/apply-ubuntu-gpt.sh /usr/local/share/bootstrap/ubuntu_layout.sfdisk /dev/mmcblk0 >> "$RESULT_FILE" 2>&1; then
    log "[bootstrap] GPT rewrite completed successfully"
    log "[bootstrap] Powering off in 10 seconds. Re-enter EDL, then run: python edl.py w system ubuntu-qcom-msm8953.img"
    sleep 10
    power_off
else
    status=$?
    log "[bootstrap] GPT rewrite failed with status $status"
    log "[bootstrap] Powering off in 30 seconds. Check /root/bootstrap-apply-ubuntu-gpt.log if you boot this image again."
    sleep 30
    power_off
fi