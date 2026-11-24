#!/usr/bin/env python3
"""Exercise Hyprvoice with a private Hyprland desktop, tray host and virtual audio.

Requires Mutter, Lua-capable Hyprland, Python Gio, PipeWire/Pulse tools, the
three build targets, and local X-ASR models. Pass a benign speech WAV. No
hardware input, output, installed services, or daily desktop are used.
"""
import argparse
from configparser import ConfigParser
import fcntl
import json
import os
from pathlib import Path
import select
import shlex
import socket
import subprocess
import tempfile
import time

from gi.repository import Gio, GLib

ROOT = Path(__file__).resolve().parents[1]
WATCHER = 'org.kde.StatusNotifierWatcher'
WATCHER_PATH = '/StatusNotifierWatcher'
DBUS = 'org.freedesktop.DBus'
MENU = 'com.canonical.dbusmenu'


def wait_for(condition, description, timeout=10):
    deadline = time.monotonic() + timeout
    context = GLib.MainContext.default()
    while time.monotonic() < deadline:
        while context.pending():
            context.iteration(False)
        value = condition()
        if value:
            return value
        time.sleep(.04)
    raise RuntimeError(f'Timed out waiting for {description}')


def call(bus, dest, path, interface, method, signature=None, args=()):
    parameters = GLib.Variant(signature, args) if signature else None
    return bus.call_sync(dest, path, interface, method, parameters, None,
                         Gio.DBusCallFlags.NO_AUTO_START, 1500, None).unpack()


class TrayWatcher:
    def __init__(self, address):
        flags = Gio.DBusConnectionFlags.AUTHENTICATION_CLIENT | Gio.DBusConnectionFlags.MESSAGE_BUS_CONNECTION
        self.bus = Gio.DBusConnection.new_for_address_sync(address, flags, None, None)
        self.items = []
        introspection = Gio.DBusNodeInfo.new_for_xml(f'''<node><interface name="{WATCHER}">
          <method name="RegisterStatusNotifierItem"><arg type="s" direction="in"/></method>
          <property name="RegisteredStatusNotifierItems" type="as" access="read"/>
          <property name="IsStatusNotifierHostRegistered" type="b" access="read"/>
          <property name="ProtocolVersion" type="i" access="read"/>
        </interface></node>''')
        self.bus.register_object(WATCHER_PATH, introspection.interfaces[0],
                                 self.register, self.property, None)
        acquired = call(self.bus, DBUS, '/org/freedesktop/DBus', DBUS,
                        'RequestName', '(su)', (WATCHER, 4))
        assert acquired == (1,), acquired

    def register(self, bus, sender, path, interface, method, parameters, invocation):
        registration, = parameters.unpack()
        item = (sender, registration) if registration.startswith('/') else (registration, '/StatusNotifierItem')
        self.items.append(item)
        invocation.return_value(None)

    def property(self, bus, sender, path, interface, name):
        return {'IsStatusNotifierHostRegistered': GLib.Variant('b', True),
                'ProtocolVersion': GLib.Variant('i', 0),
                'RegisteredStatusNotifierItems': GLib.Variant('as', [''.join(item) for item in self.items])}[name]

    def pid(self, destination):
        return call(self.bus, DBUS, '/org/freedesktop/DBus', DBUS,
                    'GetConnectionUnixProcessID', '(s)', (destination,))[0]

    def owner(self, destination):
        return call(self.bus, DBUS, '/org/freedesktop/DBus', DBUS,
                    'NameHasOwner', '(s)', (destination,))[0]


def exercise(runtime, args, children):
    env = dict(os.environ)
    for name in ('DISPLAY', 'WAYLAND_DISPLAY', 'HYPRLAND_INSTANCE_SIGNATURE',
                 'AT_SPI_BUS_ADDRESS', 'SWAYSOCK', 'SESSION_MANAGER', 'NOTIFY_SOCKET',
                 'LISTEN_PID', 'LISTEN_FDS', 'LISTEN_FDNAMES', 'DESKTOP_STARTUP_ID',
                 'XDG_ACTIVATION_TOKEN', 'PIPEWIRE_CONFIG_DIR', 'PIPEWIRE_CONFIG_NAME',
                 'PULSE_RUNTIME_PATH', 'PULSE_COOKIE'):
        env.pop(name, None)
    home = runtime / 'home'
    env.update(HOME=str(home), XDG_RUNTIME_DIR=str(runtime), XDG_CONFIG_HOME=str(runtime / 'config'),
               XDG_CACHE_HOME=str(runtime / 'cache'), XDG_DATA_HOME=str(runtime / 'data'),
               XDG_STATE_HOME=str(runtime / 'state'), TMPDIR=str(runtime),
               DBUS_SESSION_BUS_ADDRESS=f'unix:path={runtime}/bus',
               DBUS_SYSTEM_BUS_ADDRESS=f'unix:path={runtime}/no-system-bus',
               PIPEWIRE_RUNTIME_DIR=str(runtime), PIPEWIRE_REMOTE='pipewire-0',
               PULSE_SERVER=f'unix:{runtime}/pulse/native',
               GTK_A11Y='none', GTK_IM_MODULE='none', GDK_BACKEND='wayland',
               GSETTINGS_BACKEND='memory',
               HYPRVOICE_CONFIG=str(runtime / 'hyprvoice.json'))

    def spawn(argv, name, **kwargs):
        with (runtime / f'{name}.log').open('w') as log:
            child = subprocess.Popen(argv, env=env, stderr=log, text=True,
                                     stdout=kwargs.pop('stdout', log), **kwargs)
        children.append(child)
        return child

    def run(*argv, timeout=5):
        return subprocess.run(argv, env=env, capture_output=True, text=True,
                              check=True, timeout=timeout).stdout

    def hypr(*argv):
        return run('hyprctl', *argv)

    def check(name, ok):
        assert ok, name
        print('PASS', name, flush=True)

    bus_config = runtime / 'session.conf'
    bus_config.write_text(f'''<busconfig>
      <type>session</type><auth>EXTERNAL</auth><listen>{env['DBUS_SESSION_BUS_ADDRESS']}</listen>
      <policy context="default">
        <allow send_destination="*"/><allow receive_sender="*"/><allow own="*"/>
      </policy>
    </busconfig>''')
    spawn(['dbus-daemon', '--nofork', '--config-file=' + str(bus_config)], 'dbus')
    wait_for((runtime / 'bus').is_socket, 'private D-Bus')
    watcher = TrayWatcher(env['DBUS_SESSION_BUS_ADDRESS'])

    tools = home / '.local/bin'
    tools.mkdir(parents=True)
    key_log = runtime / 'bindings.log'
    command = tools / 'hyprvoice'
    # Use the checked-in binding file as-is and observe every invocation it makes.
    command.write_text('#!/bin/sh\nprintf "%s\\n" "$*" >> ' + shlex.quote(str(key_log)) +
                       '\nexec ' + shlex.quote(str(args.binary)) + ' "$@"\n')
    command.chmod(0o700)
    env['PATH'] = str(tools) + os.pathsep + env['PATH']
    desktop = ConfigParser(interpolation=None)
    desktop.read(ROOT / 'config/hyprvoice.desktop')
    launcher = shlex.split(desktop['Desktop Entry']['Exec'])

    spawn(['mutter', '--headless', '--wayland', '--no-x11', '--wayland-display=parent',
           '--virtual-monitor', '1280x800'], 'mutter')
    wait_for((runtime / 'parent').is_socket, 'private Mutter')
    env['WAYLAND_DISPLAY'] = 'parent'
    hypr_config = runtime / 'hyprland.lua'
    hypr_config.write_text('''hl.monitor({output="",mode="1280x800",position="auto",scale=1})
    hl.config({animations={enabled=false},misc={disable_hyprland_logo=true,disable_splash_rendering=true}})
    ''' + (ROOT / 'config/hyprvoice.lua').read_text())
    spawn(['Hyprland', '-c', str(hypr_config)], 'hyprland')
    env['HYPRLAND_INSTANCE_SIGNATURE'] = wait_for(
        lambda: next((p.name for p in (runtime / 'hypr').glob('*') if (p / '.socket.sock').is_socket()), None),
        'private Hyprland control socket')
    env['WAYLAND_DISPLAY'] = wait_for(
        lambda: next((p.name for p in runtime.glob('wayland-*') if p.is_socket()), None),
        'private Hyprland display')
    spawn(['pipewire'], 'pipewire')
    wait_for((runtime / 'pipewire-0').is_socket, 'private PipeWire')
    # Only policy is loaded. No ALSA, Bluetooth, or camera detection.
    spawn(['wireplumber', '--profile', 'policy'], 'wireplumber')
    spawn(['pipewire-pulse'], 'pipewire-pulse')
    wait_for((runtime / 'pulse/native').is_socket, 'private Pulse socket')
    run('pactl', 'load-module', 'module-null-sink', 'sink_name=hv_test_sink', 'rate=16000', 'channels=1')
    run('pactl', 'load-module', 'module-remap-source', 'master=hv_test_sink.monitor', 'source_name=hv_test_source')

    def audio_nodes():
        return [entry['info']['props'] for entry in json.loads(run('pw-dump'))
                if entry['type'] == 'PipeWire:Interface:Node']

    assert [p['node.name'] for p in audio_nodes() if p.get('media.class', '').startswith('Audio/Source')] == ['hv_test_source']

    config = json.loads((ROOT / 'config/config.example.json').read_text())
    config.update(audio_source='hv_test_source', auto_commit=False, detailed_review=False,
                  context={'enabled': False})
    for key in ('streaming_model', 'offline_model', 'vad_model'):
        config[key] = str(Path(config[key]).expanduser())
    config_file = Path(env['HYPRVOICE_CONFIG'])
    config_file.write_text(json.dumps(config))

    keyboard = spawn([args.keyboard], 'keyboard', stdin=subprocess.PIPE, stdout=subprocess.PIPE)

    def keyboard_reply(expected):
        assert select.select([keyboard.stdout], [], [], 4)[0], 'Virtual keyboard timed out'
        assert keyboard.stdout.readline().strip() == expected

    keyboard_reply('READY')

    def key(value):
        keyboard.stdin.write(value + '\n')
        keyboard.stdin.flush()
        keyboard_reply('OK')

    editor = spawn([args.editor, runtime / 'editor.txt', 'Synthetic target.'], 'editor')

    def focused_pid():
        return json.loads(hypr('-j', 'activewindow')).get('pid')

    wait_for(lambda: focused_pid() == editor.pid, 'test editor focus')

    def status():
        return json.loads(run(args.binary, 'status'))['state']

    def settings():
        return json.loads(run(args.binary, 'settings-status'))

    def capture_active():
        return any(p.get('node.name') == 'hyprvoice' for p in audio_nodes())

    def overlays():
        return [layer for monitor in json.loads(hypr('-j', 'layers')).values()
                for level in monitor['levels'].values() for layer in level
                if layer.get('namespace') == 'hyprvoice']

    control = runtime / 'hyprvoice/control.sock'
    control.parent.mkdir(mode=0o700)

    def off(label):
        # Model a still-listening legacy socket without any systemd mutation.
        assert not control.exists(), f'{label}: a resident socket is still present'
        key_log.write_text('')
        with socket.socket(socket.AF_UNIX) as listener:
            listener.bind(str(control))
            listener.listen()
            key('down F8')
            wait_for(lambda: 'press' in key_log.read_text().splitlines(), 'F8 press dispatch')
            key('up F8')
            wait_for(lambda: 'release' in key_log.read_text().splitlines(), 'F8 release dispatch')
            check(label + ' F8 bypasses the dormant socket',
                  not select.select([listener], [], [], .5)[0])
        control.unlink()
        check(label + ' F8 leaves no resident, tray, overlay or capture',
              all(not watcher.owner(name) for name, _ in watcher.items) and
              not control.exists() and not overlays() and not capture_active() and
              focused_pid() == editor.pid)

    off('Cold')
    # A process can win the launch lock before the control socket is ready.
    with (control.parent / 'lock').open('a') as starting:
        fcntl.flock(starting, fcntl.LOCK_EX)
        off('Before IPC readiness')
    no_runtime = {key: value for key, value in env.items() if key != 'XDG_RUNTIME_DIR'}
    for action in ('press', 'release'):
        reply = subprocess.run([args.binary, action], env=no_runtime, capture_output=True, timeout=5)
        check(f'Cold {action} is silent even without a runtime directory',
              reply.returncode == 0 and not reply.stdout and not reply.stderr)
    resident = spawn(launcher, 'first-launcher')
    destination, item_path = wait_for(lambda: watcher.items[0] if watcher.items else None,
                                      'first tray registration')
    item_props = call(watcher.bus, destination, item_path, DBUS + '.Properties',
                      'GetAll', '(s)', ('org.kde.StatusNotifierItem',))[0]
    check('First launcher owns one active tray', watcher.pid(destination) == resident.pid
          and len(watcher.items) == 1 and item_props['Status'] == 'Active')
    pixmaps = item_props['IconPixmap']
    check('Tray supplies an embedded ARGB icon without relying on a theme',
          not item_props['IconName'] and pixmaps and
          all(w > 0 and h > 0 and len(pixels) == 4 * w * h and
              0 in pixels[::4] and 255 in pixels[::4] and
              any(any(pixels[i + 1:i + 4]) for i in range(0, len(pixels), 4) if pixels[i] == 255)
              for w, h, pixels in pixmaps))
    wait_for(lambda: status()['model_ready'], 'real model readiness', timeout=30)

    def idle_focused():
        state = status()
        return state['phase'] == 'idle' and not state['busy'] and focused_pid() == editor.pid

    check('First launcher remains idle and preserves focus', idle_focused() and not settings()['open'])
    launchers = [spawn(launcher, f'repeated-launcher-{i}') for i in range(12)]
    launchers.append(spawn([args.binary, 'serve'], 'concurrent-serve'))
    check('Concurrent launchers exit successfully', all(p.wait(timeout=5) == 0 for p in launchers))
    check('Only one resident tray and no recording after concurrent launches',
          resident.poll() is None and len(watcher.items) == 1 and idle_focused() and
          not overlays() and not capture_active() and not settings()['open'])

    menu_path = item_props['Menu']

    def layout():
        _, (_, _, items) = call(watcher.bus, destination, menu_path, MENU, 'GetLayout',
                                '(iias)', (0, -1, []))
        return {properties['label']: (item, properties) for item, properties, _ in items
                if 'label' in properties}

    names = layout()

    def click(label):
        call(watcher.bus, destination, menu_path, MENU, 'Event',
             '(isvu)', (names[label][0], 'clicked', GLib.Variant('i', 0), 0))

    def mode(detailed):
        selected = '详细模式' if detailed else '极简模式'
        return (layout()[selected][1]['toggle-state'] == 1 and
                json.loads(config_file.read_text())['detailed_review'] == detailed and
                settings()['settings']['detailed_review'] == detailed)

    check('Tray presents both modes and their initial selection', mode(False))
    click('详细模式')
    wait_for(lambda: mode(True), 'persisted detailed mode and menu state')
    click('极简模式')
    wait_for(lambda: mode(False), 'persisted simple mode and menu state')
    check('Both tray mode actions update the live and persisted setting', True)

    click('设置…')

    def settings_window():
        return any(c['pid'] == resident.pid and c['title'] == 'Hyprvoice 设置与诊断'
                   for c in json.loads(hypr('-j', 'clients')))

    wait_for(settings_window, 'actual GTK Settings toplevel')
    check('Tray Settings opens the existing Settings window', settings()['open'])
    wait_for(lambda: focused_pid() == resident.pid, 'Settings window focus')
    hypr('dispatch', 'hl.dsp.window.close()')
    wait_for(lambda: not settings()['open'] and focused_pid() == editor.pid,
             'closed Settings and restored editor focus')

    key('down F8')
    wait_for(lambda: status()['phase'] == 'recording' and capture_active(), 'F8 recording from private source')
    check('F8 capture preserves editor focus', focused_pid() == editor.pid)
    run('paplay', '--device=hv_test_sink', args.audio, timeout=30)
    time.sleep(.3)
    key('up F8')
    transcript = wait_for(lambda: (state if (state := status())['phase'] == 'ready' and not state['busy'] else None),
                          'recognized speech after F8 release', timeout=30)
    check('F8 release returns a real local transcript', bool(transcript['raw'].strip()))
    print('Transcript', transcript['raw'], flush=True)
    if args.expect:
        check('Transcript contains the expected benign phrase', args.expect.casefold() in transcript['raw'].casefold())

    click('退出')
    check('Tray Quit stops resident', resident.wait(timeout=5) == 0)
    wait_for(lambda: not watcher.owner(destination), 'tray bus owner removal')
    off('After Quit')

    contenders = [spawn(launcher, f'cold-launcher-{i}') for i in range(12)]
    new_destination, _ = wait_for(lambda: watcher.items[1] if len(watcher.items) > 1 else None,
                                  'tray after simultaneous cold launchers')
    winner_pid = watcher.pid(new_destination)
    winner = next(p for p in contenders if p.pid == winner_pid)
    check('Simultaneous cold launchers leave one tray and one process',
          all(p.wait(timeout=5) == 0 for p in contenders if p is not winner) and
          winner.poll() is None and len(watcher.items) == 2 and
          focused_pid() == editor.pid and not capture_active())
    winner.terminate()
    wait_for(lambda: not control.exists(), 'control socket withdrawal after SIGTERM', timeout=2)
    # A cold winner may still be inside an opaque Sherpa model-loading call.
    check('SIGTERM withdraws the control socket and shuts down cleanly during startup',
          winner.wait(timeout=20) == 0 and not watcher.owner(new_destination))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('audio', type=Path)
    parser.add_argument('--build', type=Path, default=ROOT / 'build')
    parser.add_argument('--expect', help='Expected case-insensitive excerpt of the real transcript')
    args = parser.parse_args()
    args.audio = args.audio.resolve(strict=True)
    args.binary, args.keyboard, args.editor = (
        (args.build / name).resolve(strict=True) for name in ('hyprvoice', 'test_keyboard', 'test_editor'))
    children = []
    with tempfile.TemporaryDirectory(prefix='hv-tray-', dir='/tmp') as work:
        runtime = Path(work)
        try:
            exercise(runtime, args, children)
        except Exception:
            for log in sorted(runtime.glob('*.log')):
                if log.stat().st_size:
                    print(f'[{log.name}]\n{log.read_text(errors="replace")[-2500:]}', flush=True)
            raise
        finally:
            for process in reversed(children):
                if process.poll() is None:
                    process.terminate()
            for process in reversed(children):
                try:
                    process.wait(timeout=3)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait(timeout=3)


if __name__ == '__main__':
    main()
