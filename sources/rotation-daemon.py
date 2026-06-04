#!/usr/bin/env python3
"""
BMA253 screen auto-rotation daemon for CD-18781Y.
Reads x/y/z from IIO sysfs and calls kscreen-doctor on orientation change.

Mount orientation (empirically determined from device in normal position):
  y ≈ -1g  →  rotation.normal   (device upright, screen vertical)
  y ≈ +1g  →  rotation.inverted
  x ≈ +1g  →  rotation.left     (device rotated 90° CW from normal)
  x ≈ -1g  →  rotation.right    (device rotated 90° CCW from normal)
  |z| dominant → flat, no change

Deploy to /usr/local/bin/rotation-daemon.py
Service: /etc/xdg/systemd/user/rotation-daemon.service
"""

import time
import subprocess
import os
import signal
import sys
import logging
from pathlib import Path

IIO_BASE   = Path("/sys/bus/iio/devices/iio:device0")
SCALE_FILE = IIO_BASE / "in_accel_scale"
X_RAW      = IIO_BASE / "in_accel_x_raw"
Y_RAW      = IIO_BASE / "in_accel_y_raw"
Z_RAW      = IIO_BASE / "in_accel_z_raw"

OUTPUT     = "DSI-1"
POLL_HZ    = 1.0          # seconds between readings
THRESHOLD  = 7.0          # m/s² — must exceed this to change orientation (~0.7g)
HYSTERESIS = 2            # consecutive matching readings before switching

log_file = Path.home() / ".local/share/rotation-daemon.log"
log_file.parent.mkdir(parents=True, exist_ok=True)
logging.basicConfig(
    level=logging.INFO,
    format='%(asctime)s [%(levelname)s] %(message)s',
    handlers=[
        logging.FileHandler(log_file),
        logging.StreamHandler(sys.stdout)
    ]
)
logger = logging.getLogger(__name__)


def read_accel():
    try:
        scale = float(SCALE_FILE.read_text())
        x = int(X_RAW.read_text()) * scale
        y = int(Y_RAW.read_text()) * scale
        z = int(Z_RAW.read_text()) * scale
        return x, y, z
    except Exception as e:
        logger.warning(f"Failed to read accelerometer: {e}")
        return None, None, None


def gravity_to_orientation(x, y, z):
    """Determine orientation from dominant gravity axis."""
    ax, ay, az = abs(x), abs(y), abs(z)
    dominant = max(ax, ay, az)
    if dominant < THRESHOLD:
        return None   # ambiguous / flat
    if az == dominant:
        return None   # flat on table — no change
    if ay == dominant:
        return "normal" if y < 0 else "inverted"
    # ax is dominant
    return "left" if x > 0 else "right"


def set_rotation(orientation):
    env = {
        **os.environ,
        "XDG_RUNTIME_DIR": "/run/user/1000",
        "WAYLAND_DISPLAY": "wayland-0",
        "QT_QPA_PLATFORM": "wayland",
    }
    cmd = ["kscreen-doctor", f"output.{OUTPUT}.rotation.{orientation}"]
    try:
        r = subprocess.run(cmd, env=env, capture_output=True, timeout=5)
        if r.returncode == 0:
            logger.info(f"Rotation set to {orientation}")
        else:
            logger.warning(f"kscreen-doctor failed: {r.stderr.decode().strip()}")
    except Exception as e:
        logger.warning(f"kscreen-doctor error: {e}")


def run():
    logger.info("Rotation daemon started")
    current = "normal"
    pending = None
    pending_count = 0

    signal.signal(signal.SIGTERM, lambda s, f: sys.exit(0))
    signal.signal(signal.SIGINT,  lambda s, f: sys.exit(0))

    # Set initial rotation
    set_rotation(current)

    while True:
        x, y, z = read_accel()
        if x is not None:
            new = gravity_to_orientation(x, y, z)
            if new is not None and new != current:
                if new == pending:
                    pending_count += 1
                    if pending_count >= HYSTERESIS:
                        logger.info(f"Orientation change: {current} -> {new} (x={x:.2f} y={y:.2f} z={z:.2f})")
                        current = new
                        pending = None
                        pending_count = 0
                        set_rotation(current)
                else:
                    pending = new
                    pending_count = 1
            else:
                pending = None
                pending_count = 0

        time.sleep(POLL_HZ)


if __name__ == "__main__":
    run()
