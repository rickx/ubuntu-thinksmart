#!/usr/bin/env python3
"""
Input-idle screen blanker + ALS auto-brightness for CD-18781Y.
- Blanks after IDLE_TIMEOUT seconds of no input (writes brightness=BLANK_BRIGHTNESS)
- While screen is on: adjusts brightness from ALS every ALS_INTERVAL seconds
- Calls SimulateUserActivity on KWin's ScreenSaver every SIMULATE_INTERVAL seconds
  to prevent KWin/powerdevil from independently setting the DRM connector Off.

Deploy to /usr/local/bin/idle-blanker.py
Service: ~/.config/systemd/user/idle-blanker.service
"""

import os
import sys
import time
import glob
import math
import select
import signal
import logging
import subprocess
from pathlib import Path

IDLE_TIMEOUT    = 1800     # seconds before blanking
POLL_INTERVAL   = 1.0
BACKLIGHT_PATH  = "/sys/class/backlight/backlight/brightness"
MAX_BRIGHTNESS  = 4095
MIN_BRIGHTNESS  = 3800     # never go below ~93% in auto mode (WLED minimum ~1200)
# Blanking value: must be non-zero on Plasma 6 — writing 0 causes KWin 6 to disable
# the DRM connector at hardware level, making the panel unrecoverable without restarting
# the compositor. WLED hardware shuts off below ~1200, so 10 is physically dark but
# KWin never sees 0 and leaves the DRM connector alive.
BLANK_BRIGHTNESS = 10
ALS_PATH       = "/tmp/als_lux"
ALS_INTERVAL   = 10.0      # seconds between ALS adjustments
ALS_WAKE_COOL  = 30.0      # seconds after wake before ALS kicks in
ALS_DEADBAND   = 150       # only adjust if target differs by this much

# KWin idle prevention: call SimulateUserActivity this often to keep KWin's own
# idle timer from firing and setting DRM Off independently of our blanking logic.
SIMULATE_INTERVAL = 25.0   # seconds between SimulateUserActivity calls

log_file = Path.home() / ".local/share/idle-blanker.log"
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


def lux_to_brightness(lux):
    """Map ambient lux to backlight level (log scale)."""
    if lux <= 0:
        return MIN_BRIGHTNESS
    # log10(1)=0 → min, log10(3000)≈3.47 → max
    ratio = math.log10(max(1.0, lux)) / math.log10(3000.0)
    raw = MIN_BRIGHTNESS + int((MAX_BRIGHTNESS - MIN_BRIGHTNESS) * min(1.0, ratio))
    return max(MIN_BRIGHTNESS, min(MAX_BRIGHTNESS, raw))


def read_als():
    try:
        txt = Path(ALS_PATH).read_text().strip()
        return float(txt)
    except Exception:
        return None


def get_brightness():
    try:
        return int(Path(BACKLIGHT_PATH).read_text().strip())
    except Exception:
        return None


def set_brightness(val):
    try:
        Path(BACKLIGHT_PATH).write_text(str(val) + "\n")
        return True
    except PermissionError:
        r = subprocess.run(["sudo", "tee", BACKLIGHT_PATH],
                           input=str(val) + "\n", text=True,
                           capture_output=True, timeout=2)
        return r.returncode == 0
    except Exception as e:
        logger.warning(f"set_brightness({val}) failed: {e}")
        return False


def simulate_user_activity():
    """Tell KWin's screensaver interface that the user is active.
    This resets KWin's/powerdevil's idle timer so they never independently
    trigger DRM connector Off while our own blanking manages the backlight."""
    try:
        subprocess.run(
            ["qdbus6", "org.kde.KWin", "/ScreenSaver",
             "org.freedesktop.ScreenSaver.SimulateUserActivity"],
            capture_output=True, timeout=2
        )
    except Exception:
        pass


def open_input_devices():
    fds = []
    for path in glob.glob("/dev/input/event*"):
        try:
            fd = os.open(path, os.O_RDONLY | os.O_NONBLOCK)
            fds.append(fd)
        except Exception:
            pass
    return fds


def drain(fds):
    if not fds:
        return False
    r, _, _ = select.select(fds, [], [], 0)
    if r:
        for fd in r:
            try:
                os.read(fd, 4096)
            except Exception:
                pass
        return True
    return False


def run():
    logger.info(f"Idle blanker started — timeout={IDLE_TIMEOUT}s, ALS interval={ALS_INTERVAL}s")
    fds = open_input_devices()
    logger.info(f"Watching {len(fds)} input device(s)")

    last_input    = time.monotonic()
    last_als      = 0.0
    last_wake     = 0.0
    last_simulate = 0.0
    blanked       = False
    target_brightness = MAX_BRIGHTNESS

    def handle_signal(sig, frame):
        logger.info("Signal — restoring brightness and exiting")
        if blanked:
            set_brightness(target_brightness)
        sys.exit(0)

    signal.signal(signal.SIGTERM, handle_signal)
    signal.signal(signal.SIGINT, handle_signal)

    while True:
        time.sleep(POLL_INTERVAL)

        had_input = drain(fds)
        if had_input:
            last_input = time.monotonic()
            if blanked:
                logger.info("Input detected — restoring brightness")
                last_wake = time.monotonic()
                try:
                    restore = int(open("/tmp/user_brightness").read())
                except Exception:
                    restore = MAX_BRIGHTNESS
                set_brightness(restore)
                blanked = False

        idle_secs  = time.monotonic() - last_input
        brightness = get_brightness()

        # External restore (e.g. proximity daemon)
        if blanked and brightness is not None and brightness > BLANK_BRIGHTNESS:
            logger.info("Screen restored externally — resetting idle timer")
            last_input = time.monotonic()
            last_wake  = time.monotonic()
            try:
                open("/tmp/user_brightness", "w").write(str(brightness))
            except Exception:
                pass
            idle_secs = 0
            blanked   = False

        if not blanked:
            # ALS auto-brightness while screen is on
            now = time.monotonic()
            if now - last_als >= ALS_INTERVAL and now - last_wake >= ALS_WAKE_COOL:
                last_als = now
                lux = read_als()
                if lux is not None:
                    new_target = lux_to_brightness(lux)
                    if abs(new_target - target_brightness) >= ALS_DEADBAND:
                        logger.info(f"ALS: {lux:.1f} lux → brightness {new_target} (was {target_brightness})")
                        target_brightness = new_target
                        if brightness is not None and brightness > 0:
                            set_brightness(target_brightness)
                            try:
                                open("/tmp/user_brightness", "w").write(str(target_brightness))
                            except Exception:
                                pass

            # Blank on idle
            if idle_secs >= IDLE_TIMEOUT:
                logger.info(f"Idle for {idle_secs:.1f}s — blanking screen")
                if set_brightness(BLANK_BRIGHTNESS):
                    logger.info("Screen blanked")
                    blanked = True

        # Keep KWin's idle timer reset so powerdevil never independently
        # fires its DPMS action and sets the DRM connector Off.
        now = time.monotonic()
        if now - last_simulate >= SIMULATE_INTERVAL:
            last_simulate = now
            simulate_user_activity()


if __name__ == "__main__":
    run()
