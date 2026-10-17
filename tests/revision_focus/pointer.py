"""Physical primary pointer against actual GTK control bounds, private bus only."""
import json
from pathlib import Path
import select
import subprocess
import time


class Pointer:
    def __init__(self, namespace, output):
        self.n = namespace
        self.f = namespace
        self.env = namespace['env']
        self.evidence = namespace['evidence']
        log = (self.evidence/'physical-pointer.log').open('w')
        self.p = subprocess.Popen([str(Path(self.env['XDG_RUNTIME_DIR'])/'human-input'),'Hyprland',output],
            env=self.env,stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=log,text=True)
        namespace['children'].append((self.p,log))
        assert select.select([self.p.stdout],[],[],5)[0] and self.p.stdout.readline().strip() == 'ready'

    def send(self, message):
        self.p.stdin.write(message+'\n'); self.p.stdin.flush()
        assert select.select([self.p.stdout],[],[],5)[0] and self.p.stdout.readline().strip() == 'done', message

    def click(self, label):
        time.sleep(.2)
        app_pid = self.n['app'].pid
        control = json.loads(subprocess.check_output(['/usr/bin/python3',
            '/opt/focus-tests/native_a11y.py',str(app_pid),label],env=self.env,text=True,timeout=8))
        run = self.n['run']
        windows = [c for c in json.loads(run('hyprctl','-j','clients'))
                   if c['pid'] == app_pid and c['title'] == control.get('window_name')]
        if len(windows) == 1:
            root_x,root_y = windows[0]['at']
        else:
            layers = [layer for data in json.loads(run('hyprctl','-j','layers')).values()
                      for plane in data['levels'].values() for layer in plane
                      if layer.get('pid') == app_pid and layer.get('namespace','').startswith('hyprvoice')]
            assert len(layers) == 1, {'control':control,'layers':layers}
            root_x,root_y = layers[0]['x'],layers[0]['y']
        x,y,w,h = control['coordinate_extents']['1']
        click_x,click_y = root_x+x+w/2,root_y+y+h/2
        assert 0 <= click_x < 1280 and 0 <= click_y < 800, control
        (self.evidence/('primary-ui-'+str(time.monotonic_ns())+'.json')).write_text(json.dumps(
            {'control':control,'root_origin':[root_x,root_y],'click':[click_x,click_y]},ensure_ascii=False,indent=2))
        self.send(f'motion {round(click_x)} {round(click_y)}')
        self.send('button 272 1'); self.send('button 272 0')
        time.sleep(.15)
