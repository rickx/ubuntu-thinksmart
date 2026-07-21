#!/usr/bin/env python3
import os
import select
import subprocess
import time
from pathlib import Path

ACCEL_DIR = Path('/sys/bus/iio/devices/iio:device0')
TOUCH_EVENT = '/dev/input/event1'
ACCEL_DELTA_THRESHOLD = 40
POLL_SECONDS = 0.2
WAKE_COOLDOWN_SECONDS = 2.0

last_wake = 0.0
last_accel = None

LINUX_USER = 'ubuntu'
WAYLAND_ENV = [
    'env',
    'DISPLAY=:0',
    'WAYLAND_DISPLAY=wayland-0',
    'XDG_RUNTIME_DIR=/run/user/1000',
    'DBUS_SESSION_BUS_ADDRESS=unix:path=/run/user/1000/bus',
    'QT_QPA_PLATFORM=wayland',
]


def log(msg: str) -> None:
    print(f'[screen-wake] {msg}', flush=True)


def run_cmd(args, timeout=3):
    try:
        p = subprocess.run(args, check=False, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=timeout)
        return p.returncode == 0
    except Exception:
        return False


def run_as_user(cmd):
    return run_cmd(['runuser', '-u', LINUX_USER, '--'] + WAYLAND_ENV + cmd)


def write_brightness(val: int) -> bool:
    """Write brightness directly from root — fast path, no process spawn."""
    try:
        for p in Path('/sys/class/backlight').glob('*/brightness'):
            p.write_text(f'{val}\n')
        return True
    except Exception:
        try:
            r = subprocess.run(
                ['tee', '/sys/class/backlight/backlight/brightness'],
                input=f'{val}\n', text=True,
                capture_output=True, timeout=2
            )
            return r.returncode == 0
        except Exception:
            return False


def drm_state() -> str:
    try:
        return Path('/sys/class/drm/card0-DSI-1/dpms').read_text().strip()
    except Exception:
        return 'unknown'


def wake_display(reason: str) -> None:
    global last_wake
    now = time.time()
    if now - last_wake < WAKE_COOLDOWN_SECONDS:
        return
    last_wake = now

    # 1. Restore backlight immediately (root, no subprocess needed for write)
    ok_bl = write_brightness(4095)

    # 2. Reset KWin's idle timer so it doesn't immediately re-blank
    ok_sim = run_as_user([
        'qdbus6', 'org.kde.KWin', '/ScreenSaver',
        'org.freedesktop.ScreenSaver.SimulateUserActivity'
    ])

    # 3. Check DRM connector state
    state = drm_state()

    # 4. If DRM is still Off, restart kwin (last resort — ~5s, restores panel)
    if state == 'Off':
        log(f'DRM Off after brightness restore — restarting kwin_wayland')
        run_as_user(['kwin_wayland', '--replace'])

    log(f'wake ({reason}): bl={"ok" if ok_bl else "fail"} sim={"ok" if ok_sim else "fail"} drm={state}')


def read_accel():
    try:
        x = int((ACCEL_DIR / 'in_accel_x_raw').read_text().strip())
        y = int((ACCEL_DIR / 'in_accel_y_raw').read_text().strip())
        z = int((ACCEL_DIR / 'in_accel_z_raw').read_text().strip())
        return (x, y, z)
    except Exception:
        return None


def motion_detected(curr, prev):
    dx = abs(curr[0] - prev[0])
    dy = abs(curr[1] - prev[1])
    dz = abs(curr[2] - prev[2])
    return max(dx, dy, dz) >= ACCEL_DELTA_THRESHOLD, (dx, dy, dz)


def main() -> int:
    if not ACCEL_DIR.exists():
        log('accelerometer path missing')
        return 1

    log('starting as root service')

    try:
        touch_fd = os.open(TOUCH_EVENT, os.O_RDONLY | os.O_NONBLOCK)
        log('touch input opened')
    except Exception as e:
        log(f'cannot open touch device: {e}')
        touch_fd = None

    global last_accel
    last_accel = read_accel()
    if last_accel is not None:
        log(f'initial accel={last_accel}')

    while True:
        if touch_fd is not None:
            try:
                ready, _, _ = select.select([touch_fd], [], [], 0)
                if ready:
                    try:
                        os.read(touch_fd, 24 * 8)
                    except Exception:
                        pass
                    wake_display('touch')
            except Exception as e:
                log(f'touch read error: {e}')
                try:
                    os.close(touch_fd)
                except Exception:
                    pass
                touch_fd = None

        curr = read_accel()
        if curr is not None and last_accel is not None:
            moved, deltas = motion_detected(curr, last_accel)
            if moved:
                wake_display(f'accel d={deltas}')
        if curr is not None:
            last_accel = curr

        time.sleep(POLL_SECONDS)


if __name__ == '__main__':
    raise SystemExit(main())
