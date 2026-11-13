#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Fixed 30-second scroll/canvas workload, run on the device with --sysmem for control."""
import json
import os
import subprocess
import sys
import time
import threading
from pathlib import Path
env = os.environ.copy()
if '--session-pid' in sys.argv:
    index = sys.argv.index('--session-pid')
    if index + 1 == len(sys.argv):
        raise SystemExit('--session-pid needs a PID')
    session = Path('/proc') / str(int(sys.argv[index + 1]))
    if session.stat().st_uid != os.getuid():
        raise SystemExit('Session PID must belong to the current user')
    allowed = {'WAYLAND_DISPLAY', 'XDG_RUNTIME_DIR', 'DBUS_SESSION_BUS_ADDRESS', 'DISPLAY', 'QT_SCALE_FACTOR'}
    for entry in (session / 'environ').read_bytes().split(b'\x00'):
        key, sep, value = entry.partition(b'=')
        if sep and key.decode() in allowed:
            env[key.decode()] = value.decode()
if not env.get('XDG_RUNTIME_DIR') or not env.get('WAYLAND_DISPLAY'):
    raise SystemExit('Run in a Wayland session or supply --session-pid PID')
env.update(QT_QPA_PLATFORM='wayland', QSG_RHI_BACKEND='opengl', LANG='C.UTF-8')
env['QTWEBENGINE_CHROMIUM_FLAGS'] = '--disable-vulkan'
for k in ['FD_MESA_DEBUG', 'LIBGL_DRIVERS_PATH', 'LD_LIBRARY_PATH']:
    env.pop(k, None)
mode = 'sysmem' if '--sysmem' in sys.argv else 'patched'
if mode == 'sysmem':
    env['FD_MESA_DEBUG'] = 'sysmem'
else:
    env['LIBGL_DRIVERS_PATH'] = '/opt/thinksmart-gpu/lib/aarch64-linux-gnu/dri'
    env['LD_LIBRARY_PATH'] = '/opt/thinksmart-gpu/lib/aarch64-linux-gnu'
env['QT_FORCE_STDERR_LOGGING'] = '1'
html = (
    '<!doctype html><style>body{margin:0}div{height:60px;'
    'border-radius:8px;'
    'background:linear-gradient(45deg,#397dc7,#b8deee);'
    'margin:8px}canvas{position:fixed;'
    'top:0;'
    'right:0}</style><canvas width=300 height=180></canvas><script>for(let i=0;'
    'i<200;'
    "i++)document.body.insertAdjacentHTML('beforeend','<div>'+i+'</div>');"
    "const c=document.querySelector('canvas').getContext('2d');"
    'let start,prev,ds=[],frames=0;'
    'function tick(t){if(!start)start=t;'
    'if(t-start>5000&&prev){ds.push(t-prev);'
    'frames++}prev=t;'
    'c.clearRect(0,0,300,180);'
    'for(let i=0;'
    'i<40;'
    "i++){c.fillStyle='hsl('+i*9+' 70% 50%)';"
    'c.fillRect((t/8+i*17)%300,i*4,30,30)}scrollTo(0,(Math.sin(t/1500)+1)*2000);'
    'if(t-start<35000)requestAnimationFrame(tick);'
    'else{ds.sort((a,b)=>a-b);'
    'window.result={fps:frames/30,p95:ds[Math.floor(ds.length*.95)]}}}requestAnimationFrame(tick)</script>'
)
if '--video' in sys.argv:
    html = html.replace("const c=document.querySelector('canvas').getContext('2d');", "const source=document.querySelector('canvas');const v=document.createElement('video');v.muted=true;v.autoplay=true;v.style='position:fixed;left:0;top:0;width:300px';document.body.appendChild(v);v.srcObject=source.captureStream(30);v.play();const c=source.getContext('2d');")
    html = html.replace('t-start>5000', 't-start>1000').replace('t-start<35000', 't-start<5000').replace('fps:frames/30', 'fps:frames/4,videoTime:v.currentTime,videoWidth:v.videoWidth,h264:v.canPlayType(\'video/mp4;codecs="avc1.42E01E"\')')
qml = (
    'import QtQuick\n'
    'import QtQuick.Controls\n'
    'import QtWebEngine\n'
    'ApplicationWindow { visible:true; width:640;height:1024;title:"ThinkSmart GPU benchmark"\n'
    'WebEngineProfile{id:profile;offTheRecord:true}\n'
    'WebEngineView{id:web;anchors.fill:parent;profile:profile}\n'
    'Component.onCompleted:web.loadHtml(HTML)\n'
    'Timer{interval:37000;running:true;onTriggered:web.runJavaScript("JSON.stringify(window.result)",function(v){console.log("BENCH_RESULT",v);Qt.quit()})}\n'
    'Timer{interval:42000;running:true;onTriggered:Qt.exit(2)}\n'
    '}'
).replace('HTML', json.dumps(html))
if '--video' in sys.argv:
    qml = qml.replace('37000', '7000').replace('42000', '12000')
fd = os.memfd_create('benchmark.qml', 0)
os.write(fd, qml.encode())
p = subprocess.Popen(['/usr/bin/qml6', '-f', f'/proc/self/fd/{fd}'], env=env, pass_fds=(fd,), stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
lines = []

def drain():
    for line in p.stdout:
        if 'BENCH_RESULT' in line:
            lines.append(line.strip())
reader = threading.Thread(target=drain, daemon=True)
reader.start()

def usage(pid):
    total = 0
    pss = 0
    queue = [pid]
    visited = set()
    while queue:
        n = queue.pop()
        if n in visited:
            continue
        visited.add(n)
        base = Path('/proc') / str(n)
        try:
            parts = (base / 'stat').read_text().split(') ', 1)[1].split()
            total += int(parts[11]) + int(parts[12])
            for thread in (base / 'task').iterdir():
                try:
                    queue.extend(map(int, (thread / 'children').read_text().split()))
                except FileNotFoundError:
                    pass
            for row in (base / 'smaps_rollup').read_text().splitlines():
                if row.startswith('Pss:'):
                    pss += int(row.split()[1])
        except (FileNotFoundError, ProcessLookupError, PermissionError):
            pass
    return (total, pss)
try:
    time.sleep(5)
    first, _ = usage(p.pid)
    start = time.monotonic()
    last = first
    memory = 0
    while p.poll() is None and time.monotonic() - start < 40:
        total, mem = usage(p.pid)
        if total:
            last = total
            memory = max(memory, mem)
        time.sleep(0.5)
    p.wait(timeout=3)
    reader.join(timeout=2)
    if p.returncode != 0 or not lines:
        raise RuntimeError('QML benchmark did not produce a successful result')
    parsed = json.loads(lines[-1].split('BENCH_RESULT ', 1)[1])
    if not parsed or parsed.get('fps', 0) <= 0:
        raise RuntimeError('Missing benchmark frame samples')
    if '--video' in sys.argv:
        value = json.loads(lines[-1].split('BENCH_RESULT ', 1)[1])
        assert value['videoTime'] > 1 and value['videoWidth'] > 0
    print(json.dumps({'mode': mode, 'cpuCorePercent': round((last - first) / os.sysconf('SC_CLK_TCK') / (time.monotonic() - start) * 100, 1), 'peakPssMiB': round(memory / 1024, 1), 'result': lines, 'exit': p.returncode}))
finally:
    if p.poll() is None:
        p.kill()
        p.wait()
