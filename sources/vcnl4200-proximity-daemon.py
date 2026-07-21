#!/usr/bin/env python3
"""
Standalone VCNL4200 Proximity Sensor Daemon
Monitors proximity sensor and triggers wake when display is off and hand approaches
Independent from vp5 - runs as separate systemd service
"""

import subprocess
import time
import sys
import os
import signal
import logging
from pathlib import Path

# Configuration
I2C_BUS = 0
I2C_ADDR = 0x51
PS_CONF1_REG = 0x03
PS_DATA_REG = 0x08
ALS_CONF_REG = 0x00

# PS_CONF1: PS_IT=4T, PS_DUTY=1/40, PS_PERS=2, PS_SD=0 (active)
PS_CONF1_ENABLED = 0xCA
PS_CONF2_ENABLED = 0x08
PS_CONF1_DISABLED = 0xCB  # PS_SD bit set

# PS_MS register (0x04): high byte bits[2:0] = LED_I
# 000=50mA 001=75mA 010=100mA 011=120mA 100=140mA 101=160mA 110=180mA 111=200mA
PS_MS_REG = 0x04
PS_LED_200MA = 0x0700  # low byte (PS_CONF3) = 0x00, high byte (PS_MS) = 0x07

# Thresholds
PROXIMITY_THRESHOLD = 50   # Counts above this = object detected (ambient ~5-11, hand ~87-1044)
DEBOUNCE_COUNT = 1         # Require N consecutive readings above threshold before waking
WAKE_COOLDOWN = 10.0       # Seconds to wait before triggering wake again
POLLING_INTERVAL = 0.5    # Poll at 2 Hz
BACKLIGHT_PATH = "/sys/class/backlight/backlight/brightness"

# Blanking value must match idle-blanker.py BLANK_BRIGHTNESS.
# Proximity sensor is enabled when brightness <= this value (screen is blanked).
BLANK_BRIGHTNESS = 10

# Logging
log_file = Path.home() / ".local/share/proximity-daemon.log"
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

class ProximitySensor:
    def __init__(self):
        self.running = False
        self.sensor_enabled = False
        self.last_brightness = None
        self._above_count = 0          # consecutive readings above threshold
        self._last_wake_time = 0.0     # time of last wake trigger (for cooldown)
        signal.signal(signal.SIGTERM, self._signal_handler)
        signal.signal(signal.SIGINT, self._signal_handler)

    def _signal_handler(self, sig, frame):
        logger.info("Shutdown signal received")
        self.stop()
        sys.exit(0)

    def i2c_get(self, reg):
        """Read word from i2c register"""
        try:
            result = subprocess.run(
                ['sudo', 'i2cget', '-y', str(I2C_BUS), f'0x{I2C_ADDR:02x}', f'0x{reg:02x}', 'w'],
                capture_output=True,
                text=True,
                timeout=2
            )
            if result.returncode == 0:
                return int(result.stdout.strip(), 16)
            logger.warning(f"i2cget failed (rc={result.returncode}): {result.stderr.strip()}")
            return None
        except Exception as e:
            logger.warning(f"i2cget failed: {e}")
            return None

    def i2c_set(self, reg, val_low):
        """Write a single byte to an i2c register."""
        try:
            result = subprocess.run(
                ['sudo', 'i2cset', '-y', str(I2C_BUS), f'0x{I2C_ADDR:02x}',
                 f'0x{reg:02x}', f'0x{val_low:02x}', 'b'],
                capture_output=True,
                timeout=2
            )
            return result.returncode == 0
        except Exception as e:
            logger.warning(f"i2cset failed: {e}")
            return False

    def i2c_set_word(self, reg, word_val):
        """Write a 16-bit word to an i2c register (little-endian: low byte first)."""
        try:
            result = subprocess.run(
                ['sudo', 'i2cset', '-y', str(I2C_BUS), f'0x{I2C_ADDR:02x}',
                 f'0x{reg:02x}', f'0x{word_val:04x}', 'w'],
                capture_output=True,
                timeout=2
            )
            return result.returncode == 0
        except Exception as e:
            logger.warning(f"i2cset word failed: {e}")
            return False

    def get_brightness(self):
        """Read current backlight brightness"""
        try:
            with open(BACKLIGHT_PATH) as f:
                return int(f.read().strip())
        except Exception as e:
            logger.warning(f"Failed to read brightness: {e}")
            return None

    def enable_sensor(self):
        """Enable proximity sensor at 200mA LED current."""
        if self.sensor_enabled:
            return
        logger.info("Enabling proximity sensor")
        # Set LED current to 200mA (PS_MS reg 0x04, high byte LED_I=7)
        self.i2c_set_word(PS_MS_REG, PS_LED_200MA)
        if self.i2c_set(PS_CONF1_REG, PS_CONF1_ENABLED):
            logger.info("PS_CONF1 enabled (0xCA), LED=200mA")
            self.sensor_enabled = True
        else:
            logger.warning("Failed to enable sensor")

    def disable_sensor(self):
        """Disable proximity sensor to save power"""
        if not self.sensor_enabled:
            return
        logger.info("Disabling proximity sensor (PS_SD=1)")
        if self.i2c_set(PS_CONF1_REG, PS_CONF1_DISABLED):
            logger.info("Sensor shut down")
            self.sensor_enabled = False
        else:
            logger.warning("Failed to disable sensor")

    def read_proximity(self):
        """Read proximity data"""
        val = self.i2c_get(PS_DATA_REG)
        if val is not None:
            return val & 0xFFFF
        return None

    def trigger_wake(self):
        """Wake the display by restoring backlight brightness directly."""
        now = time.time()
        if now - self._last_wake_time < WAKE_COOLDOWN:
            logger.debug(f"Wake cooldown active ({WAKE_COOLDOWN - (now - self._last_wake_time):.1f}s remaining)")
            return
        logger.warning("=== PROXIMITY DETECTED — WAKING SCREEN ===")
        self._last_wake_time = now
        self._above_count = 0

        # Restore backlight via sudo tee.
        try:
            r = subprocess.run(
                ['sudo', 'tee', BACKLIGHT_PATH],
                input='4095\n', text=True,
                capture_output=True, timeout=3
            )
            if r.returncode == 0:
                logger.info("Screen woken via sudo tee backlight write")
            else:
                logger.warning(f"sudo tee backlight failed (rc={r.returncode}): {r.stderr.strip()}")
        except Exception as e:
            logger.warning(f"Backlight write failed: {e}")

        # Reset KWin's idle timer so it doesn't immediately re-blank.
        try:
            subprocess.run(
                ['dbus-send', '--session', '--type=method_call',
                 '--dest=org.kde.KWin', '/ScreenSaver',
                 'org.freedesktop.ScreenSaver.SimulateUserActivity'],
                capture_output=True, text=True, timeout=3
            )
        except Exception:
            pass

    def run(self):
        """Main daemon loop"""
        logger.info("Proximity daemon started")
        self.running = True

        # Initial state
        brightness = self.get_brightness()
        logger.info(f"Initial brightness: {brightness}")
        self.last_brightness = brightness

        while self.running:
            try:
                # Check brightness
                brightness = self.get_brightness()

                if brightness is not None:
                    if self.last_brightness is None or brightness != self.last_brightness:
                        logger.info(f"Brightness changed: {self.last_brightness} -> {brightness}")
                        self.last_brightness = brightness

                    # Screen is blanked (brightness at or below BLANK_BRIGHTNESS=10)
                    if brightness <= BLANK_BRIGHTNESS:
                        if not self.sensor_enabled:
                            self.enable_sensor()
                            self._above_count = 0  # reset debounce on enable

                        # Read proximity
                        prox = self.read_proximity()
                        if prox is not None:
                            logger.debug(f"PS_DATA = 0x{prox:04x} ({prox})")

                            if prox > PROXIMITY_THRESHOLD:
                                self._above_count += 1
                                logger.debug(f"Above threshold ({prox} > {PROXIMITY_THRESHOLD}), debounce {self._above_count}/{DEBOUNCE_COUNT}")
                                if self._above_count >= DEBOUNCE_COUNT:
                                    logger.warning(f"PROXIMITY: {prox} counts over {DEBOUNCE_COUNT} readings — triggering wake")
                                    self.trigger_wake()
                            else:
                                self._above_count = 0

                    # Screen is ON (brightness > BLANK_BRIGHTNESS)
                    else:
                        if self.sensor_enabled:
                            self.disable_sensor()

                time.sleep(POLLING_INTERVAL)

            except Exception as e:
                logger.error(f"Error in loop: {e}")
                time.sleep(1)

    def stop(self):
        """Shutdown gracefully"""
        self.running = False
        self.disable_sensor()
        logger.info("Daemon stopped")

if __name__ == '__main__':
    daemon = ProximitySensor()
    daemon.run()
