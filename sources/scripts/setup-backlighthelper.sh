#!/bin/sh
# Build and deploy a fixed backlighthelper for KDE powerdevil on CD-18781Y.
#
# WHY THIS IS NEEDED:
#   1. Ubuntu's stock backlighthelper binary lacks the `rawAll` fallback for
#      backlight device detection. The WLED backlight registers as BACKLIGHT_RAW
#      type; without rawAll the helper's init() fails and brightness control is
#      completely non-functional.
#
#   2. The Ubuntu polkit policy for org.kde.powerdevil.backlighthelper is missing
#      <allow_any>yes</allow_any> for the setbrightness action and has
#      allow_inactive=no. Since powerdevil's Wayland session is not always
#      flagged "active" by logind, polkit denies setbrightness before the helper's
#      Q_SLOT is ever dispatched. The polkit fix is applied below.
#
# PREREQUISITES (run as root):
#   apt install libkf5auth-dev libkf5authcore5 libkf5coreaddons-dev qtbase5-dev
#               extra-cmake-modules cmake g++
#
# POWERDEVIL SOURCE:
#   This script expects powerdevil 5.27.11 source to be at /tmp/powerdevil-5.27.11.
#   Download: https://download.kde.org/stable/plasma/5.27.11/powerdevil-5.27.11.tar.xz
#   Unpack:   tar -xJf powerdevil-5.27.11.tar.xz -C /tmp/

set -e

SRC=/tmp/powerdevil-5.27.11/daemon/backends/upower
BDIR=/tmp/bh-build
DEST=/usr/lib/kauth/libexec/backlighthelper
POLICY=/usr/share/polkit-1/actions/org.kde.powerdevil.backlighthelper.policy

if [ ! -f "$SRC/linuxbacklighthelper.cpp" ]; then
    echo "ERROR: $SRC/linuxbacklighthelper.cpp not found."
    echo "Download and unpack powerdevil 5.27.11 source to /tmp/ first."
    exit 1
fi

# ── Build ──────────────────────────────────────────────────────────────────────
mkdir -p "$BDIR"

# Create a minimal CMakeLists.txt that builds just the helper
cat > "$BDIR/CMakeLists.txt" << 'CMEOF'
cmake_minimum_required(VERSION 3.16)
project(backlighthelper)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_AUTOMOC ON)
find_package(ECM REQUIRED NO_MODULE)
set(CMAKE_MODULE_PATH ${ECM_MODULE_PATH} ${CMAKE_MODULE_PATH})
include(KDEInstallDirs)
include(KDECMakeSettings)
find_package(Qt5 REQUIRED COMPONENTS Core)
find_package(KF5Auth REQUIRED)
find_package(KF5CoreAddons REQUIRED)
add_executable(backlighthelper linuxbacklighthelper.cpp)
target_include_directories(backlighthelper PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/stub_ki18n)
target_link_libraries(backlighthelper Qt5::Core KF5::Auth KF5::CoreAddons)
kauth_install_helper_files(backlighthelper "org.kde.powerdevil.backlighthelper" root)
install(TARGETS backlighthelper DESTINATION ${KAUTH_HELPER_INSTALL_DIR})
CMEOF

# Stub out KLocalizedString (not needed for the helper binary)
mkdir -p "$BDIR/stub_ki18n"
cat > "$BDIR/stub_ki18n/KLocalizedString" << 'STEOF'
#pragma once
#include <QString>
inline QString i18n(const char *s, ...) { return QString::fromUtf8(s); }
inline QString i18nc(const char *, const char *s, ...) { return QString::fromUtf8(s); }
STEOF

cp "$SRC/linuxbacklighthelper.cpp" "$BDIR/"
cp "$SRC/linuxbacklighthelper.h"   "$BDIR/"

# Minimal powerdevil debug category stub
cat > "$BDIR/powerdevil_debug.h" << 'DBGEOF'
#pragma once
#include <QLoggingCategory>
Q_DECLARE_LOGGING_CATEGORY(POWERDEVIL)
Q_LOGGING_CATEGORY(POWERDEVIL, "org.kde.powerdevil")
DBGEOF

cd "$BDIR"
cmake . -DCMAKE_BUILD_TYPE=Release \
        -DECM_DIR=/usr/share/ECM/cmake \
        -DCMAKE_INSTALL_PREFIX=/usr 2>&1
make -j"$(nproc)" 2>&1

# ── Deploy ─────────────────────────────────────────────────────────────────────
echo "=== deploying binary ==="
cp "$DEST" "${DEST}.ubuntu-orig" 2>/dev/null || true
cp "$BDIR/backlighthelper" "$DEST"
chmod 755 "$DEST"
echo "Installed: $(sha256sum "$DEST" | cut -c1-16)  $DEST"

# ── Polkit fix ─────────────────────────────────────────────────────────────────
# Add allow_any=yes and allow_inactive=yes for the setbrightness action.
echo "=== patching polkit policy ==="

python3 - << 'PYEOF'
import re, sys

POLICY = "/usr/share/polkit-1/actions/org.kde.powerdevil.backlighthelper.policy"

with open(POLICY) as f:
    s = f.read()

# The setbrightness action currently has the wrong/incomplete defaults.
# Replace with the full allow_any block.
old = (
    "      <defaults>\n"
    "         <allow_inactive>no</allow_inactive>\n"
    "         <allow_active>yes</allow_active>\n"
    "      </defaults>\n"
    "   </action>\n"
    "   <action id=\"org.kde.powerdevil.backlighthelper.syspath\" >"
)
new = (
    "      <defaults>\n"
    "         <allow_any>yes</allow_any>\n"
    "         <allow_inactive>yes</allow_inactive>\n"
    "         <allow_active>yes</allow_active>\n"
    "      </defaults>\n"
    "   </action>\n"
    "   <action id=\"org.kde.powerdevil.backlighthelper.syspath\" >"
)

if old in s:
    s = s.replace(old, new)
    with open(POLICY, "w") as f:
        f.write(s)
    print("polkit policy patched: setbrightness now has allow_any=yes")
elif "allow_any" in s and "setbrightness" in s:
    # Check if already patched
    idx = s.find("setbrightness")
    chunk = s[idx:idx+300]
    if "allow_any" in chunk:
        print("polkit policy already has allow_any for setbrightness — no change needed")
    else:
        print("ERROR: unexpected policy format, manual edit required")
        sys.exit(1)
else:
    print("ERROR: setbrightness action pattern not found in policy file")
    sys.exit(1)
PYEOF

# ── Done ───────────────────────────────────────────────────────────────────────
echo
echo "Setup complete. Kill and restart powerdevil to apply:"
echo "  pkill -f org_kde_powerdevil; systemctl --user restart plasma-powerdevil"
