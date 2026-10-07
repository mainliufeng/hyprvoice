"""Publish only this test's AT-SPI address on its private session bus.

Chromium's ATK bridge discovers org.a11y.Bus instead of using the test's
AT_SPI_BUS_ADDRESS environment override. Never connect to the daily session.
"""
import os
from pathlib import Path
from gi.repository import Gio, GLib

runtime = Path(os.environ['XDG_RUNTIME_DIR'])
assert runtime.resolve() != Path(f'/run/user/{os.getuid()}')
address = os.environ['AT_SPI_BUS_ADDRESS']
assert address.startswith('unix:path=' + str(runtime) + '/')
assert os.environ['DBUS_SESSION_BUS_ADDRESS'] != f'unix:path=/run/user/{os.getuid()}/bus'
interface = Gio.DBusNodeInfo.new_for_xml('''<node>
<interface name="org.a11y.Bus"><method name="GetAddress"><arg type="s" direction="out"/></method></interface>
<interface name="org.a11y.Status"><property name="IsEnabled" type="b" access="read"/><property name="ScreenReaderEnabled" type="b" access="read"/></interface>
</node>''')
bus = Gio.bus_get_sync(Gio.BusType.SESSION, None)

def method(conn, sender, path, iface, name, parameters, invocation):
    invocation.return_value(GLib.Variant('(s)', (address,)))

def get_property(conn, sender, path, iface, name):
    # Mirror the current desktop: accessibility enabled, screen reader off.
    return GLib.Variant('b', name == 'IsEnabled')

for iface in interface.interfaces:
    bus.register_object('/org/a11y/bus', iface, method, get_property, None)
Gio.bus_own_name_on_connection(bus, 'org.a11y.Bus', Gio.BusNameOwnerFlags.NONE,
                             lambda *_: print('READY', flush=True), None)
GLib.MainLoop().run()
