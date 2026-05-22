#!/bin/bash
# Build out-of-tree TAS5782M modules in WSL from the repo layout.

set -e

SCRIPT_DIR="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"
REPO_ROOT="$(CDPATH= cd -- "$SCRIPT_DIR/../.." && pwd)"
DRIVER_DIR="$REPO_ROOT/sources/driver"
PREBUILT_DIR="$REPO_ROOT/prebuilt"
WSLSRC="/home/fleverato/tas5782m-src"
KSRC="/home/fleverato/kernel-build-v619"

mkdir -p "$PREBUILT_DIR"

echo "=== Syncing source files to WSL build dir ==="
for f in tas5782m.c tas5782m.h tas5782m_priv.h tas5782m_dbg.h \
          tas5782m_tables.c tas5782m_dbg.c \
          tas5782m_dbgbuild.c tas5782m_tables_dbgbuild.c Makefile; do
    cp "$DRIVER_DIR/$f" "$WSLSRC/$f"
    echo "  copied $f"
done

echo ""
echo "=== Building modules ==="
make ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- CC=clang-18 \
     KBUILD_MODPOST_WARN=1 \
     -C "$KSRC" M="$WSLSRC" modules 2>&1

echo ""
echo "=== Copying artifacts ==="
cp "$WSLSRC/snd-soc-tas5782m-dbg.ko" "$PREBUILT_DIR/"
sha256sum "$PREBUILT_DIR/snd-soc-tas5782m-dbg.ko"
modinfo "$WSLSRC/snd-soc-tas5782m-dbg.ko" | grep -E 'vermagic|alias'

echo ""
echo "=== Build complete ==="
