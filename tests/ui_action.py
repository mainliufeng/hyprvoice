#!/usr/bin/env python3
"""Operate real GTK accessibility buttons only inside an isolated QA session.

Run with the system Python and PyGObject. No app state is injected.
"""
import argparse
import os
import json
from pathlib import Path
import gi
from gi.repository import Gio, GLib

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('button', nargs='?')
parser.add_argument('--scroll-end', action='store_true')
args = parser.parse_args()
runtime = os.environ.get('XDG_RUNTIME_DIR', '')
if not runtime or Path(runtime).resolve() == Path(f'/run/user/{os.getuid()}'):
    parser.error('Refusing to operate the daily desktop')
address = os.environ['AT_SPI_BUS_ADDRESS']
bus = Gio.DBusConnection.new_for_address_sync(address,
    Gio.DBusConnectionFlags.AUTHENTICATION_CLIENT | Gio.DBusConnectionFlags.MESSAGE_BUS_CONNECTION,
    None, None)

def call(destination, path, interface, method, parameters=None):
    return bus.call_sync(destination, path, interface, method, parameters, None,
                         Gio.DBusCallFlags.NONE, 3000, None).unpack()

seen = set()
def find(destination, path):
    if (destination, path) in seen or len(seen) > 300:
        return None
    seen.add((destination, path))
    try:
        name = call(destination, path, 'org.freedesktop.DBus.Properties', 'Get',
                    GLib.Variant('(ss)', ('org.a11y.atspi.Accessible', 'Name')))[0]
        if args.scroll_end:
            try:
                maximum = call(destination, path, 'org.freedesktop.DBus.Properties', 'Get',
                    GLib.Variant('(ss)', ('org.a11y.atspi.Value', 'MaximumValue')))[0]
                if maximum > 0:
                    return destination, path
            except GLib.Error:
                pass
        if args.button and name == args.button:
            actions = call(destination, path, 'org.a11y.atspi.Action', 'GetActions')[0]
            if actions:
                return destination, path
        children = call(destination, path, 'org.a11y.atspi.Accessible', 'GetChildren')[0]
        for child in children:
            result = find(*child)
            if result:
                return result
    except GLib.Error:
        pass
    return None

node = find('org.a11y.atspi.Registry', '/org/a11y/atspi/accessible/root')
if not node:
    raise SystemExit('Accessibility control not found: ' + str(args.button or 'scrollbar'))
if args.scroll_end:
    maximum = call(*node, 'org.freedesktop.DBus.Properties', 'Get',
        GLib.Variant('(ss)', ('org.a11y.atspi.Value', 'MaximumValue')))[0]
    call(*node, 'org.freedesktop.DBus.Properties', 'Set',
         GLib.Variant('(ssv)', ('org.a11y.atspi.Value', 'CurrentValue', GLib.Variant('d', maximum))))
    print(json.dumps({'scrolled_to_end': True, 'maximum': maximum}))
    raise SystemExit(0)
result = call(*node, 'org.a11y.atspi.Action', 'DoAction', GLib.Variant('(i)', (0,)))[0]
if not result:
    raise SystemExit('GTK button rejected action')
print(json.dumps({'button': args.button, 'activated': True}, ensure_ascii=False))
