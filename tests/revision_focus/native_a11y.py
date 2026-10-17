"""Return actual visible control geometry for one PID on the private AT-SPI bus."""
import json
import os
from pathlib import Path
import sys
from gi.repository import Gio, GLib

runtime = Path(os.environ['XDG_RUNTIME_DIR'])
assert runtime.resolve() != Path(f'/run/user/{os.getuid()}')
address = os.environ['AT_SPI_BUS_ADDRESS']
assert address.startswith('unix:path='+str(runtime)+'/')
pid, wanted = int(sys.argv[1]), sys.argv[2]
bus = Gio.DBusConnection.new_for_address_sync(address,
    Gio.DBusConnectionFlags.AUTHENTICATION_CLIENT | Gio.DBusConnectionFlags.MESSAGE_BUS_CONNECTION,None,None)
def call(dest,path,interface,method,parameters=None):
    return bus.call_sync(dest,path,interface,method,parameters,None,Gio.DBusCallFlags.NONE,1500,None).unpack()
seen = set()
def walk(dest,path,window_name=None):
    if (dest,path) in seen or len(seen)>300:
        return None
    seen.add((dest,path))
    try:
        name = call(dest,path,'org.freedesktop.DBus.Properties','Get',GLib.Variant('(ss)',('org.a11y.atspi.Accessible','Name')))[0]
        states = call(dest,path,'org.a11y.atspi.Accessible','GetState')[0]
        role = call(dest,path,'org.a11y.atspi.Accessible','GetRoleName')[0]
        if role in ('frame','window','dialog'):
            window_name = name
        interfaces = call(dest,path,'org.a11y.atspi.Accessible','GetInterfaces')[0]
        actionable = 'org.a11y.atspi.Action' in interfaces or 'org.a11y.atspi.EditableText' in interfaces
        if name == wanted and states[0] & (1<<25) and actionable:
            geometry = call(dest,path,'org.a11y.atspi.Component','GetExtents',GLib.Variant('(u)',(0,)))[0]
            if geometry[2]>0 and geometry[3]>0:
                positions = {}
                for coordinate in (0,1,2):
                    try:
                        positions[str(coordinate)] = call(dest,path,'org.a11y.atspi.Component','GetExtents',GLib.Variant('(u)',(coordinate,)))[0]
                    except GLib.Error:
                        pass
                return {'name':name,'geometry':geometry,'coordinate_extents':positions,'interfaces':interfaces,'window_name':window_name}

        for child in call(dest,path,'org.a11y.atspi.Accessible','GetChildren')[0]:
            result = walk(*child,window_name)
            if result:
                return result
    except GLib.Error:
        pass
for dest,path in call('org.a11y.atspi.Registry','/org/a11y/atspi/accessible/root','org.a11y.atspi.Accessible','GetChildren')[0]:
    owner = call('org.freedesktop.DBus','/org/freedesktop/DBus','org.freedesktop.DBus','GetConnectionUnixProcessID',GLib.Variant('(s)',(dest,)))[0]
    if owner == pid:
        result = walk(dest,path)
        if result:
            print(json.dumps(result,ensure_ascii=False)); raise SystemExit(0)
raise SystemExit('Actual visible control missing: '+wanted)
