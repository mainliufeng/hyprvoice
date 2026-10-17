"""Real App on real controlled seats. ASR/HTTP alone are explicit test doubles."""
import hashlib
import json
import os
from pathlib import Path
import select
import subprocess
import time
from types import SimpleNamespace

OWNER = 'hyprvoice-focus-native-owner-0000000000001'


def run(namespace):
    f = SimpleNamespace(**namespace)
    base = Path(os.environ['HV_NATIVE_BASE'])
    route_only = os.environ.get('HV_NATIVE_ROUTE_ONLY') == '1'
    f.result.update(native_seats=True, route_only=route_only,
                    geometry_only=os.environ.get("HV_NATIVE_GEOMETRY_ONLY") == "1",
                    real_overlay_editor_tested=False, launcher_tested=False)
    env = dict(f.env)
    helper_log = (f.evidence/'human-input.log').open('w')
    human = subprocess.Popen([str(base/'human-input'),'Hyprland','human'],env=env,
        stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=helper_log,text=True)
    f.children.append((human,helper_log))
    assert select.select([human.stdout],[],[],5)[0] and human.stdout.readline().strip() == 'ready'

    def send(message):
        human.stdin.write(message+'\n'); human.stdin.flush()
        assert select.select([human.stdout],[],[],5)[0] and human.stdout.readline().strip() == 'done', message

    def cli(*args):
        return json.loads(subprocess.check_output(['/opt/cornice/bin/cornice','desktop',*map(str,args)],
                              env=env,text=True,stderr=subprocess.PIPE,timeout=20))

    def ctl(*args):
        value = subprocess.check_output(['/opt/fork/build-release/hyprctl/hyprctl',*args],env=env,text=True,timeout=5)
        return json.loads(value) if '-j' in args else value

    def route():
        return ctl('-j','seat','input-target-v2')

    def clients():
        return ctl('-j','clients')

    def photo(name):
        time.sleep(.25)
        subprocess.run(['grim','-o','human',str(f.evidence/(name+'.png'))],env=env,check=True,timeout=5)

    def show(name, control=True):
        state = cli('state',name)
        shown = ctl('-j','seat','present',name,state['display'],'human','current',OWNER)
        assert shown['active'], shown
        if control:
            controlled = ctl('-j','seat','present-control',OWNER,'yes')
            assert controlled['humanControl'], controlled
        return shown

    def hide():
        ctl('seat','unpresent',OWNER)

    paths = {}
    def launch(name, seat):
        path = base/(name+'.txt')
        paths[name] = path
        cli('launch',seat,'--','/usr/bin/python3','/opt/cornice-src/test/agent-desktop-client.py',name,str(path))
        return f.wait(lambda: next((c for c in clients() if c['title'] == name),None))

    def click_entry(name, seat):
        ctl('seat','presentation',OWNER)  # renew the explicit native presentation lease
        window = f.wait(lambda: next((c for c in clients() if c['title'] == name),None))
        geometry = f.wait(lambda: json.loads(paths[name].with_suffix('.geometry').read_text()) if paths[name].with_suffix('.geometry').exists() else None)
        offset = cli('state',seat)['position']
        x,y,w,h = geometry['entry']
        send(f"motion {round(window['at'][0]-offset[0]+x+w/2)} {round(window['at'][1]-offset[1]+y+h/2)}")
        send('button 272 1'); send('button 272 0')
        try:
            f.wait(lambda: (r if (r := route()).get('window',{}).get('address') == window['address'] else None))
        except Exception:
            (f.evidence/'native-click-failure.json').write_text(json.dumps(
                {'target':window,'physical_route':route(),'geometry':geometry,
                 'seat_state':cli('state',seat)},ensure_ascii=False,indent=2))
            photo('native-click-timeout')
            raise
        return window

    def content(name):
        return paths[name].read_text() if paths[name].exists() else ''

    def record(body, output):
        f.set_transcripts(body); f.plan('success',output=output)
        f.begin(); return f.finish()

    def revise(output, after_return=False):
        f.set_transcripts('synthetic revision request')
        index = f.plan('success',output=output)
        if route_only:
            reply = f.command('command')
        else:
            send('key 67 1')  # Actual primary wl_keyboard F9 while in takeover.
            try:
                f.wait(lambda: f.state()['busy'])
                reply = (0, {})
            except Exception:
                reply = (1, {'state': f.state()})
                send('key 67 0')
        if reply[0] != 0:
            (f.evidence/'native-focus-start-failure.json').write_text(json.dumps(
                {'reply':reply,'state':f.state(),'physical_route':route()},ensure_ascii=False,indent=2))
        f.check('secondary-return-f9-can-start-original-target' if after_return else 'secondary-initial-f9-can-start-original-target',reply[0] == 0)
        source,target = f.wait(f.ports,seconds=4)
        f.run('pw-link',source,target); f.phase('recording'); time.sleep(.2)
        if route_only:
            f.command('command-release')
        else:
            send('key 67 0')
        ready = f.phase('ready')
        return ready,index

    def click_ui(label):
        # Component geometry locates the actual App widget, but activation uses
        # the controlled primary physical pointer. AT-SPI DoAction alone could
        # activate an overlay that is not composited into the native view.
        app_pid = namespace['app'].pid
        control = json.loads(subprocess.check_output(['/usr/bin/python3',
            '/opt/focus-tests/native_a11y.py',str(app_pid),label],env=env,text=True,timeout=8))
        # Wayland GTK layer surfaces report SCREEN=0,0. WINDOW coordinates
        # are real, and must be combined with the compositor's actual surface.
        x,y,w,h = control['coordinate_extents']['1']
        windows = [c for c in clients() if c['pid'] == app_pid and
                   c['title'] == control.get('window_name')]
        offset = [0,0]
        if len(windows) == 1:
            root_x,root_y = windows[0]['at']
            offset = cli('state','agent1')['position']
        else:
            layers = []
            for monitor, data in ctl('-j','layers').items():
                for plane in data['levels'].values():
                    for layer in plane:
                        if layer.get('pid') == app_pid and layer.get('namespace','').startswith('hyprvoice'):
                            layers.append((monitor,layer))
            assert len(layers) == 1, {'reason':'Cannot unambiguously locate actual App surface','control':control,'layers':layers}
            monitor, layer = layers[0]
            root_x,root_y = layer['x'],layer['y']
            if monitor != 'human':
                offset = cli('state','agent1')['position']
        click_x,click_y = root_x+x+w/2-offset[0], root_y+y+h/2-offset[1]
        assert 0 <= click_x < 1280 and 0 <= click_y < 800, {'control':control,'click':[click_x,click_y]}
        (f.evidence/('native-ui-'+str(time.monotonic_ns())+'.json')).write_text(json.dumps(
            {'control':control,'root_origin':[root_x,root_y],'view_offset':offset,'click':[click_x,click_y]},ensure_ascii=False,indent=2))
        send(f'motion {round(click_x)} {round(click_y)}')
        send('button 272 1'); send('button 272 0')
        time.sleep(.15)

    def shell(name,*args):
        digest = hashlib.sha256(env['HYPRLAND_INSTANCE_SIGNATURE'].encode()).hexdigest()[:8]
        shell_env = dict(env,CORNICE_SHELL_SOCKET=str(f.runtime/('cs-'+digest+'-'+name+'.sock')))
        return subprocess.check_output(['/opt/cornice/bin/cornice',*args],env=shell_env,text=True,stderr=subprocess.PIPE,timeout=10).strip()

    try:
        f.boot(auto=False)
        ctl('eval','hl.bind("F9",hl.dsp.exec_cmd("/opt/hv/build/hyprvoice command"),{})')
        ctl('eval','hl.bind("F9",hl.dsp.exec_cmd("/opt/hv/build/hyprvoice command-release"),{release=true})')
        for name in ('agent1','agent2'):
            cli('create',name,'--virtual-output','1280x800'); cli('resume',name); cli('view-workspace',name,'1')
        launch('focus-agent1','agent1'); launch('focus-agent1-other','agent1'); launch('focus-agent2','agent2')
        (f.evidence/'native-seat-list.json').write_text(json.dumps(ctl('-j','seat','list'),indent=2))
        (f.evidence/'native-agent1-state.json').write_text(json.dumps(cli('state','agent1'),indent=2))
        primary_before = f.first_path.read_text()
        show('agent1'); first = click_entry('focus-agent1','agent1')
        r = route()
        (f.evidence/'native-input-target-v2.json').write_text(json.dumps(r,indent=2))
        f.check('actual-physical-route-is-controlled-agent1',r['seatName'] == 'agent1' and r['window']['pid'] == first['pid'])
        original = 'synthetic agent1 pending body.'
        revised = 'synthetic agent1 revised body.'
        ready = record(original,original)
        f.check('actual-app-captures-secondary-and-retains-body',ready['effective_text'] == original and content('focus-agent1') == '' and f.first_path.read_text() == primary_before)
        if route_only:
            geometry = subprocess.run(['/usr/bin/python3','/opt/focus-tests/native_a11y.py',
                str(namespace['app'].pid),'复制'],env=env,text=True,capture_output=True,timeout=8)
            (f.evidence/'native-copy-geometry.json').write_text(json.dumps(
                {'returncode':geometry.returncode,'stdout':geometry.stdout,'stderr':geometry.stderr},ensure_ascii=False,indent=2))
            f.check('private-native-control-geometry-query-works',geometry.returncode == 0)
        photo('native-01-agent1-real-app-ready')
        if os.environ.get('HV_NATIVE_GEOMETRY_ONLY') == '1':
            return
        ready,_ = revise(revised)
        f.check('actual-secondary-app-revision-needs-confirmation',ready['effective_text'] == revised and content('focus-agent1') == '')
        f.check('secondary-explicit-input-command-succeeds',f.command('insert-current')[0] == 0)
        f.phase('idle'); f.wait(lambda: content('focus-agent1') == revised)
        f.check('secondary-input-only-reaches-agent1',content('focus-agent1-other') == '' and content('focus-agent2') == '' and f.first_path.read_text() == primary_before)
        photo('native-02-agent1-confirmed-once')

        click_entry('focus-agent1','agent1')
        pending = 'synthetic launcher-protected pending body.'
        record(pending,pending)
        shell('agent1','ipc','shell','summon','cn.launcher','{}')
        f.wait(lambda: not route().get('allowed',False))
        before = {name:content(name) for name in paths}
        f.check('real-launcher-rejects-revision',f.command('command')[0] == 1 and f.state()['effective_text'] == pending)
        f.command('command-release')
        f.check('real-launcher-rejects-input-without-hidden-fallback',f.command('insert-current')[0] == 1 and {name:content(name) for name in paths} == before)
        f.result['launcher_tested'] = True
        photo('native-03-real-launcher-blocks-app')
        shell('agent1','ipc','shell','hide','cn.launcher')
        f.wait(lambda: not any(w['id'] == 'cn.launcher' and w['open'] for w in json.loads(shell('agent1','ipc','shell','windows'))))
        time.sleep(.2)
        f.command('discard'); f.phase('idle')

        click_entry('focus-agent1','agent1')
        record(original,original)
        show('agent2'); click_entry('focus-agent2','agent2')
        f.check('different-seat-rejects-revision-and-retains-body',f.command('command')[0] == 1 and f.state()['effective_text'] == original)
        f.command('command-release')
        f.check('different-seat-input-cannot-steal-result',f.command('insert-current')[0] == 1 and content('focus-agent2') == '')
        f.command('discard'); f.phase('idle')

        show('agent1'); click_entry('focus-agent1','agent1')
        record(original,original)
        edit_body = original
        if not route_only:
            edit_body = 'synthetic body edited inside real secondary overlay.'
            click_ui('编辑正文')
            f.wait(lambda: any(w['name'] == '编辑完整正文' and w['showing'] for w in f.accessible()['widgets']))
            click_ui('编辑完整正文')
            before_edit = content('focus-agent1')
            f.key('ctrl a'); f.key('type '+edit_body)
            f.check('native-overlay-receives-typing-without-editing-underlying-app',f.state().get('draft_text') == edit_body and content('focus-agent1') == before_edit)
            f.check('secondary-draft-blocks-global-f9',f.command('command')[0] == 1)
            f.command('command-release')
            f.check('secondary-draft-blocks-input',f.command('insert-current')[0] == 1)
            click_ui('完成编辑')
            f.wait(lambda: f.state()['effective_text'] == edit_body)
            f.result['real_overlay_editor_tested'] = True
        click_entry('focus-agent1','agent1')
        token_before = route()['token']
        hide()
        f.check('primary-cannot-steal-pending-agent1-body',f.command('command')[0] == 1 and f.state()['effective_text'] == edit_body)
        f.command('command-release')
        f.check('primary-cannot-receive-agent1-result',f.command('insert-current')[0] == 1 and f.first_path.read_text() == primary_before)
        show('agent1'); click_entry('focus-agent1-other','agent1')
        f.check('different-window-same-seat-cannot-revise',f.command('command')[0] == 1 and f.state()['effective_text'] == edit_body)
        f.command('command-release')
        click_entry('focus-agent1','agent1')
        f.check('native-away-back-actually-changes-route-token',route()['token'] != token_before)
        ready,index = revise('synthetic final original-seat result.', after_return=True)
        user = json.loads(json.loads(f.server.bodies[index])['messages'][1]['content'])
        f.check('native-return-revision-uses-saved-body',user['original_body'] == edit_body)
        f.command('undo-revision')
        f.check('native-undo-restores-pre-revision-body',f.state()['effective_text'] == edit_body)
        if route_only:
            f.check('native-return-input-rechecks-original-target',f.command('insert-current')[0] == 0)
        else:
            click_ui('确认输入')
        f.phase('idle'); f.wait(lambda: content('focus-agent1') == revised + edit_body)
        f.check('native-return-input-only-original-seat-window-once',content('focus-agent1-other') == '' and content('focus-agent2') == '' and f.first_path.read_text() == primary_before and f.command('insert-current')[0] == 1)
        photo('native-04-returned-original-seat-confirmed')
        f.check('native-suite-has-no-unplanned-http',f.server.unexpected == 0)
    finally:
        try:
            hide()
        except Exception:
            pass
