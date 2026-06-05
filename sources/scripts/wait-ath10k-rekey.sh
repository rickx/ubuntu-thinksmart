#!/bin/sh
# Monitor for GTK rekey events after the patched ath10k_core was loaded.
# Load time reference: look for the firmware version line to find load timestamp.
# Runs for up to 15 minutes, prints matching lines immediately.

LOAD_TS=$(dmesg | grep "qca9379 hw1.0 sdio" | tail -1 | grep -o '^\[[^]]*\]' | tr -d '[]' | awk '{print $1}')
echo "=== ath10k rekey monitor ==="
echo "Module load timestamp: ${LOAD_TS:-unknown}"
date -Is || date
echo ""
echo "Watching for 900s (15 min)..."
echo "Expected: 'failed to install key' warn WITHOUT 'deauthenticating' following it"
echo ""

end=$(($(date +%s) + 900))
LAST=$(dmesg | wc -l)

while [ $(date +%s) -lt $end ]; do
    NEW=$(dmesg | wc -l)
    if [ "$NEW" -gt "$LAST" ]; then
        dmesg | tail -$((NEW - LAST)) | grep -iE 'ath10k.*install key|wlan0.*key|wlan0.*deauth|wlan0.*associated|wlan0.*authenticate'
        LAST=$NEW
    fi
    sleep 2
done

echo ""
echo "=== 15-min window done ==="
echo "--- all post-load key/deauth events ---"
dmesg | awk -v ts="${LOAD_TS:-0}" '{if ($1+0 > ts+0) print}' | \
  grep -iE 'install key|deauth|wlan0.*key|wlan0.*assoc|wlan0.*auth' || echo "(none)"
