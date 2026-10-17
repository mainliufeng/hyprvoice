"""Compose existing real Cornice harness and existing real Hyprvoice App fixture."""
import ast
import json
import os
from pathlib import Path
import subprocess
import sys

assert os.environ.get('HV_PRIVATE_DESKTOP') == '1'
sys.path.insert(0, '/opt/cornice-src/test')
import desktop_harness as h
h.ENV.update(NO_AT_BRIDGE='0', GTK_A11Y='atspi')
try:
    config = h.BASE / 'config/cornice'
    config.mkdir(parents=True, exist_ok=True)
    (config/'config.json').write_text(json.dumps({'agentDesktop':{'enabled':True},
        'background':{'enabled':False}, 'bar':{'layout':{'left':[],'center':[],'right':[]}},
        'idle':{'lock':0,'screenOffAc':0,'screenOffBattery':0,'dimAc':0,'dimBattery':0,
                'lockOnSleep':False,'lockOnLockSignal':False,'lockOnLidClose':False}}))
    h.initialize()
    caps = h.ctl('seat capabilities', True)
    (Path('/tmp/evidence')/'native-broker-endpoints.json').write_text(json.dumps({
        'legacy_signature_socket_exists': (h.RT/'cornice'/h.ENV['HYPRLAND_INSTANCE_SIGNATURE']/'desktop.sock').is_socket(),
        'current_hashed_socket_exists': h.broker_socket().is_socket()},indent=2))
    assert 'physical-input-target-v2' in caps['features'], caps
    answer = subprocess.check_output(['gdbus','call','--session','--dest','org.a11y.Bus',
        '--object-path','/org/a11y/bus','--method','org.a11y.Bus.GetAddress'],env=h.ENV,text=True)
    h.ENV['AT_SPI_BUS_ADDRESS'] = ast.literal_eval(answer)[0]
    assert h.ENV['AT_SPI_BUS_ADDRESS'].startswith('unix:path='+str(h.RT)+'/')
    tools = h.RT/'bin'
    tools.mkdir(exist_ok=True)
    (tools/'hyprctl').write_text('''#!/usr/bin/python3
import os,sys,json
args=sys.argv[1:]
if len(args)>=3 and args[:2]==['dispatch','focuswindow']:
    args=['dispatch','hl.dsp.focus({window='+json.dumps(args[2])+'})']
os.execv('/opt/fork/build-release/hyprctl/hyprctl',['hyprctl',*args])
''')
    (tools/'hyprctl').chmod(0o700)
    h.ENV['PATH'] = str(tools)+':/opt/cornice/bin:/usr/bin:/bin'
    h.ENV['HYPRVOICE_REVISION_QA'] = '1'
    h.ENV['HV_NATIVE_BASE'] = str(h.BASE)
    # Build the existing controlled primary-seat keyboard/pointer helper inside
    # this private writable namespace, from unmodified compatible fork sources.
    for source, name in [('virtual-keyboard-unstable-v1','virtual-keyboard'),
                         ('wlr-virtual-pointer-unstable-v1','virtual-pointer')]:
        for mode, ext in [('client-header','h'),('private-code','c')]:
            subprocess.run(['wayland-scanner',mode,str(h.FORK/'protocols'/f'{source}.xml'),
                            str(h.BASE/f'{name}.{ext}')],check=True)
    flags = subprocess.check_output(['pkg-config','--cflags','--libs','wayland-client','xkbcommon'],text=True).split()
    subprocess.run(['cc','-I'+str(h.BASE),str(h.FORK/'hyprtester/multiseat/input.c'),
                    str(h.BASE/'virtual-keyboard.c'),str(h.BASE/'virtual-pointer.c'),
                    '-o',str(h.BASE/'human-input'),*flags],check=True)
    # This report is separate from App checks: successful compositor startup is
    # necessary, but never counted as the App focus acceptance itself.
    (Path('/tmp/evidence')/'native-capabilities.json').write_text(json.dumps(caps,indent=2))
    subprocess.run(['/usr/bin/python3','/opt/focus-tests/native_bootstrap.py',str(h.RT),'/tmp/evidence'],
                   env=h.ENV,check=True,timeout=360)
finally:
    h.cleanup()
