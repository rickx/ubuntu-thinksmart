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

# Safe config: duty=1/320, LED=50mA, 16-bit HD
PS_CONF1_ENABLED = 0xCA
PS_CONF2_ENABLED = 0x08
PS_CONF1_DISABLED = 0xCB  # PS_SD bit set

# Thresholds
PROXIMITY_THRESHOLD = 50   # Counts above this = object detected (ambient ~5-11, hand ~87-1044)
DEBOUNCE_COUNT = 1         # Require N consecutive readings above threshold before waking
WAKE_COOLDOWN = 10.0       # Seconds to wait before triggering wake again
POLLING_INTERVAL = 0.5    # Poll at 2 Hz
BACKLIGHT_PATH = "/sys/class/backlight/backlight/brightness"

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
    
    def i2c_set(self, reg, val_low, val_high=None):
        """Write to i2c register (byte or word mode)"""
        try:
            if val_high is not None:
                # Word mode: i2cset -y 0 0x51 REG VAL_LOW b
                result = subprocess.run(
                    ['sudo', 'i2cset', '-y', str(I2C_BUS), f'0x{I2C_ADDR:02x}', 
                     f'0x{reg:02x}', f'0x{val_low:02x}', 'b'],
                    capture_output=True,
                    timeout=2
                )
            else:
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
    
    def get_brightness(self):
        """Read current backlight brightness"""
        try:
            with open(BACKLIGHT_PATH) as f:
                return int(f.read().strip())
        except Exception as e:
            logger.warning(f"Failed to read brightness: {e}")
            return None
    
    def enable_sensor(self):
        """Enable proximity sensor"""
        if self.sensor_enabled:
            return
        logger.info("Enabling proximity sensor")
        if self.i2c_set(PS_CONF1_REG, PS_CONF1_ENABLED):
            logger.info("PS_CONF1 enabled (0xCA)")
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
    
    def _get_x_env(self):
        """Discover DISPLAY and XAUTHORITY from the running KDE/X session"""
        import glob
        # Find a process that has DISPLAY set in the user session
        for name in ['plasmashell', 'kwin_x11', 'startplasma', 'Xorg']:
            try:
                result = subprocess.run(
                    ['pgrep', '-u', 'user', name],
                    capture_output=True, text=True, timeout=2
                )
                pid = result.stdout.strip().split('\n')[0]
                if not pid:
                    continue
                with open(f'/proc/{pid}/environ', 'rb') as f:
                    env = dict(
                        item.split(b'=', 1)
                        for item in f.read().split(b'\0')
                        if b'=' in item
                    )
                display = env.get(b'DISPLAY', b'').decode()
                xauth = env.get(b'XAUTHORITY', b'').decode()
                if display:
                    return display, xauth
            except Exception:
                pass
        return ':0', os.path.expanduser('~/.Xauthority')

    def trigger_wake(self):
        """Wake the display via D-Bus SimulateUserActivity (Wayland/KDE)"""
        now = time.time()
        if now - self._last_wake_time < WAKE_COOLDOWN:
            logger.debug(f"Wake cooldown active ({WAKE_COOLDOWN - (now - self._last_wake_time):.1f}s remaining)")
            return
        logger.warning("=== PROXIMITY DETECTED — WAKING SCREEN ===")
        self._last_wake_time = now
        self._above_count = 0

        # Method 1: KDE screensaver SimulateUserActivity (works on Wayland)
        try:
            result = subprocess.run(
                ['dbus-send', '--session', '--type=method_call',
                 '--dest=org.kde.screensaver', '/ScreenSaver',
                 'org.freedesktop.ScreenSaver.SimulateUserActivity'],
                capture_output=True, text=True, timeout=3
            )
            if result.returncode == 0:
                logger.info("Screen woken via KDE SimulateUserActivity")
                return
            logger.warning(f"dbus-send kde returned {result.returncode}: {result.stderr.strip()}")
        except Exception as e:
            logger.warning(f"dbus-send kde failed: {e}")

        # Method 2: freedesktop screensaver SimulateUserActivity
        try:
            result = subprocess.run(
                ['dbus-send', '--session', '--type=method_call',
                 '--dest=org.freedesktop.ScreenSaver', '/ScreenSaver',
                 'org.freedesktop.ScreenSaver.SimulateUserActivity'],
                capture_output=True, text=True, timeout=3
            )
            if result.returncode == 0:
                logger.info("Screen woken via freedesktop SimulateUserActivity")
                return
            logger.warning(f"dbus-send fdo returned {result.returncode}: {result.stderr.strip()}")
        except Exception as e:
            logger.warning(f"dbus-send fdo failed: {e}")

        # Method 3: direct backlight as last resort
        try:
            with open(BACKLIGHT_PATH, 'w') as f:
                f.write('4095\n')
            logger.info("Screen woken via direct backlight write")
        except Exception as e:
            logger.warning(f"All wake methods failed: {e}")
    
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
                    
                    # Screen is OFF (brightness = 0)
                    if brightness == 0:
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
                    
                    # Screen is ON (brightness > 0)
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
