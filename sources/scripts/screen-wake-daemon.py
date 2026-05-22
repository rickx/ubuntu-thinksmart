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


def log(msg: str) -> None:
    print(f'[screen-wake] {msg}', flush=True)


def run_cmd(args, timeout=3):
    try:
        process = subprocess.run(
            args,
            check=False,
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
            timeout=timeout,
        )
        return process.returncode == 0
    except Exception:
        return False


def run_as_user(cmd):
    return run_cmd([
        'runuser', '-u', 'user', '--', 'env',
        'DISPLAY=:0',
        'WAYLAND_DISPLAY=wayland-0',
        'XDG_RUNTIME_DIR=/run/user/1000',
        'DBUS_SESSION_BUS_ADDRESS=unix:path=/run/user/1000/bus',
        'QT_QPA_PLATFORM=wayland',
    ] + cmd)


def wake_display(reason: str) -> None:
    global last_wake
    now = time.time()
    if now - last_wake < WAKE_COOLDOWN_SECONDS:
        return

    ok_primary = run_as_user(['kscreen-doctor', '--dpms', 'on'])
    ok_qdbus = run_as_user(['qdbus', 'org.kde.KWin', '/KWin', 'showDesktop', 'false'])

    ok_backlight = False
    for path in Path('/sys/class/backlight').glob('*/bl_power'):
        try:
            path.write_text('0')
            ok_backlight = True
        except Exception:
            pass

    _ = run_cmd(['loginctl', 'unlock-session'], timeout=2)

    last_wake = now
    log(
        f'wake triggered ({reason}), '
        f'dpms={"ok" if ok_primary else "fail"}, '
        f'qdbus={"ok" if ok_qdbus else "fail"}, '
        f'backlight={"ok" if ok_backlight else "fail"}'
    )


def read_accel():
    try:
        accel_x = int((ACCEL_DIR / 'in_accel_x_raw').read_text().strip())
        accel_y = int((ACCEL_DIR / 'in_accel_y_raw').read_text().strip())
        accel_z = int((ACCEL_DIR / 'in_accel_z_raw').read_text().strip())
        return (accel_x, accel_y, accel_z)
    except Exception:
        return None


def motion_detected(curr, prev):
    delta_x = abs(curr[0] - prev[0])
    delta_y = abs(curr[1] - prev[1])
    delta_z = abs(curr[2] - prev[2])
    return max(delta_x, delta_y, delta_z) >= ACCEL_DELTA_THRESHOLD, (delta_x, delta_y, delta_z)


def main() -> int:
    if not ACCEL_DIR.exists():
        log('accelerometer path missing')
        return 1

    log('starting as root service')

    try:
        touch_fd = os.open(TOUCH_EVENT, os.O_RDONLY | os.O_NONBLOCK)
        log('touch input opened')
    except Exception as exc:
        log(f'cannot open touch device: {exc}')
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
            except Exception as exc:
                log(f'touch read error: {exc}')
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