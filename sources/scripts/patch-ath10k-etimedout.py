#!/usr/bin/env python3
"""
Binary-patch ath10k_core.ko to swallow -ETIMEDOUT from ath10k_install_key.

Patch location:
  VA 0x681c, file offset 0x8614 in .text (section file_offset=0x1df8, VMA=0)

  Before: 5b 00 00 14  →  b 0x6988  (goto exit, propagates ETIMEDOUT → mac80211 deauth)
  After:  5a 00 00 14  →  b 0x6984  (mov w23,wzr → return 0, deauth suppressed)

Source equivalent (mac.c ath10k_set_key):
  After ath10k_install_key fails and ath10k_warn logs it, instead of
  jumping to exit (which returns -ETIMEDOUT to mac80211), jump to the
  cleanup block that zeroes the return value and unlocks.
"""
import hashlib
import sys

SRC = "/tmp/ath10k_core-stock.ko"
DST = "/tmp/ath10k_core-patched.ko"
PATCH_OFFSET = 0x8614
OLD_BYTE = 0x5b
NEW_BYTE = 0x5a

with open(SRC, "rb") as f:
    data = bytearray(f.read())

actual = data[PATCH_OFFSET]
if actual != OLD_BYTE:
    print(f"ERROR: expected 0x{OLD_BYTE:02x} at offset 0x{PATCH_OFFSET:x}, got 0x{actual:02x}")
    print("Wrong module version or already patched?")
    sys.exit(1)

data[PATCH_OFFSET] = NEW_BYTE

with open(DST, "wb") as f:
    f.write(bytes(data))

src_sha = hashlib.sha256(open(SRC, "rb").read()).hexdigest()[:16]
dst_sha = hashlib.sha256(open(DST, "rb").read()).hexdigest()[:16]

print(f"Patched offset 0x{PATCH_OFFSET:x}: 0x{OLD_BYTE:02x} -> 0x{NEW_BYTE:02x}")
print(f"Stock  SHA256: {src_sha}...")
print(f"Patched SHA256: {dst_sha}...")
print(f"Written to {DST}")
