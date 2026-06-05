#!/bin/sh
# Install the binary-patched ath10k_core.ko into the module tree.
# ath10k_sdio.ko is left untouched (stock).
set -e

MODULE_DIR=/lib/modules/6.19.5-msm8953/kernel/drivers/net/wireless/ath/ath10k
PATCHED=/opt/ath10k-patched/ath10k_core.ko

echo "=== install patched ath10k_core.ko ==="
date -Is || date

echo
echo "--- backup check ---"
if [ ! -f "$MODULE_DIR/ath10k_core.ko.orig" ]; then
    cp "$MODULE_DIR/ath10k_core.ko" "$MODULE_DIR/ath10k_core.ko.orig"
    echo "  created .orig backup"
else
    echo "  .orig already exists (from previous session) — not overwriting"
fi

echo
echo "--- installing ---"
cp "$PATCHED" "$MODULE_DIR/ath10k_core.ko"
depmod -a 6.19.5-msm8953

echo
echo "--- verify ---"
modinfo "$MODULE_DIR/ath10k_core.ko" | grep -E 'filename|vermagic'
echo "  .orig sha256: $(sha256sum $MODULE_DIR/ath10k_core.ko.orig | cut -c1-16)"
echo "  active sha256: $(sha256sum $MODULE_DIR/ath10k_core.ko | cut -c1-16)"

echo
echo "=== done — patched ath10k_core.ko active on next boot ==="
echo "(currently running patched version was loaded via insmod — reboot not needed)"
