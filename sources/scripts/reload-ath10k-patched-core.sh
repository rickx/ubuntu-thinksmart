#!/bin/sh
# Hot-swap ath10k_core.ko with the patched binary.
# Run this from a USB-network SSH session — WiFi will drop briefly.
# ath10k_sdio stays stock throughout.
set -e

STOCK_DIR=/lib/modules/6.19.5-msm8953/kernel/drivers/net/wireless/ath/ath10k
PATCHED=/opt/ath10k-patched/ath10k_core.ko

echo "=== hot-swap ath10k_core (patched) ==="
date -Is || date

echo
echo "--- pre-swap state ---"
lsmod | grep '^ath10k' || echo "(no ath10k modules loaded)"
ip -brief link show wlan0 2>/dev/null || echo "(wlan0 not visible yet)"

echo
echo "--- unloading ath10k_sdio then ath10k_core ---"
rmmod ath10k_sdio 2>/dev/null || true
rmmod ath10k_core 2>/dev/null || true

echo "--- loading patched ath10k_core ---"
insmod "$PATCHED"

echo "--- loading stock ath10k_sdio ---"
insmod "$STOCK_DIR/ath10k_sdio.ko"

echo
echo "--- waiting 8s for firmware + wlan0 ---"
sleep 8

echo
echo "--- post-swap lsmod ---"
lsmod | grep '^ath10k'

echo
echo "--- wlan0 state ---"
ip -brief link show wlan0 2>/dev/null || echo "(wlan0 absent)"
iw dev wlan0 link 2>/dev/null | head -6 || true

echo
echo "--- dmesg: last 30 ath10k lines ---"
dmesg | grep -i ath10k | tail -30

echo
echo "=== done — watch dmesg for GTK rekey events (expect warn but NO deauth) ==="
