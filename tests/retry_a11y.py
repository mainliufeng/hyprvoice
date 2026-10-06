#!/usr/bin/env python3
"""Inspect/activate only a named test PID on an explicitly private AT-SPI bus."""
import json
import os
import sys
from pathlib import Path

from gi.repository import Gio, GLib

runtime = Path(os.environ['XDG_RUNTIME_DIR'])
assert runtime.resolve() != Path(f'/run/user/{os.getuid()}')
address = os.environ['AT_SPI_BUS_ADDRESS']
assert address.startswith('unix:path=' + str(runtime) + '/')
pid = int(sys.argv[1])
wanted = sys.argv[2] if len(sys.argv) > 2 else None
focus = bool(wanted and wanted.startswith('focus:'))
if focus:
    wanted = wanted[6:]
bus = Gio.DBusConnection.new_for_address_sync(
    address, Gio.DBusConnectionFlags.AUTHENTICATION_CLIENT |
    Gio.DBusConnectionFlags.MESSAGE_BUS_CONNECTION, None, None)


def call(dest, path, interface, method, parameters=None):
    return bus.call_sync(dest, path, interface, method, parameters, None,
                         Gio.DBusCallFlags.NONE, 1500, None).unpack()


children = call('org.a11y.atspi.Registry', '/org/a11y/atspi/accessible/root',
                'org.a11y.atspi.Accessible', 'GetChildren')[0]
values, seen = [], set()
activated = False
action_errors = []


def walk(dest, path):
    global activated
    if (dest, path) in seen or len(seen) > 256:
        return
    seen.add((dest, path))
    try:
        name = call(dest, path, 'org.freedesktop.DBus.Properties', 'Get',
                    GLib.Variant('(ss)', ('org.a11y.atspi.Accessible', 'Name')))[0]
        states = call(dest, path, 'org.a11y.atspi.Accessible', 'GetState')[0]
        # ATSPI_STATE_ENABLED=8, SENSITIVE=24, SHOWING=25. Keep raw states too.
        entry = None
        focused = bool(states[0] & (1 << 12))
        if name:
            values.append({'name': name, 'states': states,
                           'enabled': bool(states[0] & (1 << 8)),
                           'sensitive': bool(states[0] & (1 << 24)),
                           'showing': bool(states[0] & (1 << 25)),
                           'focused': focused})
            entry = values[-1]
        if wanted == name and not activated:
            try:
                activated = bool(call(dest, path, 'org.a11y.atspi.Component',
                                      'GrabFocus')[0]) if focus else bool(call(
                    dest, path, 'org.a11y.atspi.Action', 'DoAction',
                    GLib.Variant('(i)', (0,)))[0])
            except GLib.Error as error:
                action_errors.append(str(error))
        for child in call(dest, path, 'org.a11y.atspi.Accessible', 'GetChildren')[0]:
            focused = bool(walk(*child)) or focused
        if entry is not None:
            entry['focus_within'] = focused
        return focused
    except GLib.Error:
        pass


for dest, path in children:
    actual = call('org.freedesktop.DBus', '/org/freedesktop/DBus',
                  'org.freedesktop.DBus', 'GetConnectionUnixProcessID',
                  GLib.Variant('(s)', (dest,)))[0]
    if actual == pid:
        walk(dest, path)
print(json.dumps({'widgets': values, 'activated': activated, 'action_errors': action_errors}, ensure_ascii=False))
if wanted and not activated:
    raise SystemExit(1)
