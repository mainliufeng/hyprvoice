"""Private rendered App QA; invoked only inside run-revision-desktop.sh."""
import ast
import json
import os
from pathlib import Path
import subprocess
import time

assert os.environ.get('HV_PRIVATE_DESKTOP') == '1'
rt = Path('/tmp/qa')
rt.mkdir(mode=0o700, exist_ok=True)
env = dict(os.environ, XDG_RUNTIME_DIR=str(rt))
children = []

def launch(args, name):
    child = subprocess.Popen(args, env=env, stdout=(rt / (name + '.log')).open('w'), stderr=subprocess.STDOUT)
    children.append(child)
    return child

def wait(fn):
    for _ in range(250):
        found = fn()
        if found:
            return found
        if any(p.poll() is not None for p in children):
            raise RuntimeError('Private desktop process exited; inspect private logs')
        time.sleep(.1)
    raise TimeoutError('Private desktop startup timed out')

try:
    bus = subprocess.check_output(['dbus-daemon', '--session', '--fork', '--print-address=1'], env=env, text=True).strip()
    env['DBUS_SESSION_BUS_ADDRESS'] = bus
    launch(['mutter', '--headless', '--wayland', '--no-x11', '--wayland-display=parent', '--virtual-monitor', '1280x800'], 'mutter')
    wait(lambda: (rt / 'parent').is_socket())
    env['WAYLAND_DISPLAY'] = 'parent'
    config = rt / 'hyprland.lua'
    config.write_text('hl.monitor({output="",mode="1280x800",position="auto",scale=1})\nhl.config({animations={enabled=false},misc={disable_hyprland_logo=true,disable_splash_rendering=true},debug={enable_stdout_logs=true}})\n')
    launch(['/opt/Hyprland', '-c', str(config)], 'hyprland')
    env['HYPRLAND_INSTANCE_SIGNATURE'] = wait(lambda: next((p.name for p in (rt/'hypr').glob('*') if (p/'.socket.sock').is_socket()), None))
    def ctl(*args):
        return subprocess.check_output(['/usr/bin/hyprctl', *args], env=env, text=True)
    ctl('output', 'create', 'headless', 'HV-QA')
    ctl('eval', 'hl.monitor({output="HV-QA",mode="1280x800",position="0x0",scale=1})')
    for monitor in json.loads(ctl('-j', 'monitors')):
        if monitor['name'] != 'HV-QA':
            ctl('eval', 'hl.monitor({output='+json.dumps(monitor['name'])+',disabled=true})')
    env['WAYLAND_DISPLAY'] = wait(lambda: next((p.name for p in rt.glob('wayland-*') if p.is_socket()), None))
    ctl('dispatch', 'hl.dsp.focus({monitor="HV-QA"})')
    answer = subprocess.check_output(['gdbus', 'call', '--session', '--dest', 'org.a11y.Bus', '--object-path', '/org/a11y/bus', '--method', 'org.a11y.Bus.GetAddress'], env=env, text=True)
    env['AT_SPI_BUS_ADDRESS'] = ast.literal_eval(answer)[0]
    assert env['AT_SPI_BUS_ADDRESS'].startswith('unix:path='+str(rt)+'/')
    tools = rt/'bin'
    tools.mkdir(exist_ok=True)
    (tools/'hyprctl').write_text('''#!/usr/bin/python3
import os,sys
args=sys.argv[1:]
if len(args)>=3 and args[:2]==['dispatch','focuswindow']:
    import json
    args=['dispatch','hl.dsp.focus({window='+json.dumps(args[2])+'})']
os.execv('/usr/bin/hyprctl',['hyprctl',*args])
''')
    (tools/'hyprctl').chmod(0o700)
    env.update(PATH=str(tools)+':/usr/bin:/bin')
    if os.environ.get('HV_TEST_SUITE') == 'review':
        env['HYPRVOICE_REVIEW_QA'] = '1'
    elif os.environ.get('HV_TEST_SUITE') == 'capture':
        env['HYPRVOICE_CAPTURE_QA'] = '1'
    elif os.environ.get('HV_TEST_SUITE') != 'retry':
        env['HYPRVOICE_REVISION_QA'] = '1'
    subprocess.run(['/usr/bin/python3', '/opt/hv/tests/retry_desktop_test.py', str(rt), '/tmp/evidence'], env=env, check=True)
finally:
    for child in reversed(children):
        if child.poll() is None:
            child.terminate()
    for child in reversed(children):
        try:
            child.wait(timeout=4)
        except subprocess.TimeoutExpired:
            child.kill()
