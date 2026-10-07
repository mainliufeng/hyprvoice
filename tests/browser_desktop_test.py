#!/usr/bin/env python3
"""Production browser input acceptance, private Chromium/GTK + zero-source PW + HTTP.

Requires an empty, explicitly isolated Hyprland/AT-SPI session and existing
build targets. Refuses the daily runtime; all text/audio here is synthetic.
"""
import collections
import hashlib
import http.server
import json
import os
import signal
import subprocess
import sys
import threading
import time
from pathlib import Path

repo = Path(__file__).resolve().parent.parent
candidate=repo/'build/hyprvoice'
runtime, evidence = map(Path, sys.argv[1:])
env = dict(os.environ)
assert runtime.resolve() != Path(f'/run/user/{os.getuid()}')
assert env['XDG_RUNTIME_DIR'] == str(runtime)
assert env['AT_SPI_BUS_ADDRESS'].startswith('unix:path=' + str(runtime) + '/')
evidence.mkdir(exist_ok=True)
children, checks = [], []
result = {'checks': checks, 'real_microphone': False, 'external_model_calls': False, 'chrome_profile_fresh': True, 'source_commit': subprocess.check_output(['git','rev-parse','HEAD'],cwd=repo,text=True).strip(), 'source_worktree_modified': bool(subprocess.check_output(['git','status','--porcelain'],cwd=repo,text=True).strip()), 'binary_sha256': hashlib.sha256(candidate.read_bytes()).hexdigest()}
app = None
server = None


def spawn(argv, name, extra=None):
    log = (evidence / (name + '.log')).open('w')
    p = subprocess.Popen(argv, env=dict(env, **(extra or {})), stdin=subprocess.PIPE, stdout=log,
                         stderr=subprocess.STDOUT)
    children.append((p, log))
    return p


def wait(fn, seconds=8):
    end = time.monotonic() + seconds
    while time.monotonic() < end:
        value = fn()
        if value:
            return value
        time.sleep(.04)
    raise RuntimeError('Private retry acceptance timed out')


def run(*argv):
    return subprocess.check_output(argv, env=env, text=True, timeout=4)


def command(*argv):
    r = subprocess.run([str(candidate), *argv], env=env,
                       capture_output=True, text=True, timeout=5)
    return r.returncode, json.loads(r.stdout)


def state():
    return command('status')[1]['state']


def phase(name):
    return wait(lambda: (s if (s := state())['phase'] == name and
                        (name in ('recording', 'retrying') or not s['busy']) else None))


def check(name, ok):
    assert ok, name
    checks.append(name)


def accessible(click=None):
    args = ['/usr/bin/python3', str(repo / 'tests/retry_a11y.py'), str(app.pid)]
    if click:
        args.append(click)
    reply = subprocess.run(args, env=env, capture_output=True, text=True, timeout=8)
    value = json.loads(reply.stdout)
    if reply.returncode and not click:
        raise RuntimeError('Private widget inspection failed')
    return value


def label(name):
    def visible():
        value = accessible()
        return value if any(w['name'] == name and w['showing']
                            for w in value['widgets']) else None
    return wait(visible)


def click(name):
    wait(lambda: any(w['name'] == name and w['showing'] and w['sensitive']
                     for w in accessible()['widgets']))
    value = accessible(name)
    if not value['activated']:
        (evidence/'action-failure.json').write_text(json.dumps(
            {'state': state(), 'widgets': value}, ensure_ascii=False, indent=2))
    assert value['activated']


def photo(name):
    time.sleep(.35)
    subprocess.run(['grim', '-o', 'HV-QA', str(evidence / (name + '.png'))],
                   env=env, check=True, timeout=4)


def key(text):
    count = (evidence / 'keyboard.log').read_text().count('OK')
    keyboard.stdin.write((text + '\n').encode())
    keyboard.stdin.flush()
    wait(lambda: (evidence / 'keyboard.log').read_text().count('OK') > count)
    time.sleep(.10)


def focus(editor):
    run('hyprctl', 'dispatch', 'focuswindow', 'pid:' + str(editor.pid))
    time.sleep(.15)


def editor(text, name, password=False, two=False, readonly=False):
    path = runtime / (name + '.txt')
    p = spawn([str(repo / 'build/test_editor'), str(path), text] +
              (['password'] if password else ['two'] if two else ['readonly'] if readonly else []), name)
    wait(lambda: any(v['pid'] == p.pid for v in json.loads(run('hyprctl', 'clients', '-j'))))
    focus(p)
    key('ctrl End')
    return p, path


class Handler(http.server.BaseHTTPRequestHandler):
    def do_POST(self):
        body = self.rfile.read(int(self.headers['Content-Length']))
        assert self.headers['Authorization'] == 'Bearer synthetic-retry-key'
        with self.server.lock:
            self.server.bodies.append(body)
            mode = self.server.plan.popleft() if self.server.plan else 'unexpected'
            if mode == 'unexpected':
                self.server.unexpected += 1
        gate = mode if isinstance(mode, threading.Event) else None
        if gate:
            gate.wait(8)
        if mode == 'timeout':
            time.sleep(2)
        content = self.server.output
        response = {'choices': [{'message': {'content': content}}]}
        if mode == 'empty':
            response = {'choices': [{'message': {'content': ''}}]}
        self.send_response(500 if mode in ('failure', 'unexpected') else 200)
        self.send_header('Content-Type', 'application/json')
        self.end_headers()
        try:
            self.wfile.write(json.dumps(response, ensure_ascii=False).encode())
        except (BrokenPipeError, ConnectionResetError):
            pass
        if gate:
            self.server.returned.set()

    def log_message(self, *args):
        pass


class Server(http.server.ThreadingHTTPServer):
    daemon_threads = True


def plan(*modes, output='合成结果：不会支付2400元。'):
    with server.lock:
        assert not server.plan
        server.plan.extend(modes)
        server.output = output
        server.returned.clear()
        return len(server.bodies)


def boot(timeout=5, auto=True, restore=False):
    global app
    if app and app.poll() is None:
        assert command('quit')[0] == 0
        app.wait(timeout=6)
    config['llm']['timeout_seconds'] = timeout
    config['auto_commit'] = auto
    config['clipboard_restore'] = restore
    config_path.write_text(json.dumps(config, ensure_ascii=False))
    app = spawn([str(candidate), 'serve'], 'app-' + str(len(children)))
    wait(lambda: (runtime / 'hyprvoice/control.sock').is_socket())
    phase('idle')


def set_transcripts(*texts):
    transcript_plan.write_text(json.dumps(list(texts), ensure_ascii=False))
    decode_count.write_text('0')


def ports():
    nodes = json.loads(run('pw-dump'))
    streams = [n for n in nodes if n['type'] == 'PipeWire:Interface:Node' and
               n['info']['props'].get('node.name') == 'hyprvoice']
    outputs = run('pw-link', '-o').splitlines()
    inputs = run('pw-link', '-i').splitlines()
    if streams and not any(p.startswith('hyprvoice:') for p in inputs):
        run('pw-cli', 'set-param', str(streams[0]['id']), 'PortConfig',
            '{ direction: Input mode: dsp format: { mediaType: audio mediaSubtype: raw '
            'format: F32P channels: 1 position: [ MONO ] } }')
        return None
    source = next((p for p in outputs if p.startswith('hv-qa-silence:')), None)
    target = next((p for p in inputs if p.startswith('hyprvoice:')), None)
    return (source, target) if source and target else None


def begin(mode='start'):
    started = time.monotonic()
    code, reply = command(mode)
    if code:
        (evidence/'public-start-failure.json').write_text(json.dumps(reply,ensure_ascii=False,indent=2))
    assert code == 0
    result.setdefault('start_command_ms', []).append(round((time.monotonic()-started)*1000, 2))
    source, target = wait(ports, seconds=3)
    run('pw-link', source, target)
    phase('recording')
    time.sleep(.20)


def finish(expected='ready'):
    assert command('stop')[0] == 0
    return phase(expected)


try:
    # These modules cannot discover an ALSA microphone or connect daily audio.
    pw_config = runtime / 'pipewire-qa.conf'
    pw_config.write_text('''context.properties = { core.daemon = true core.name = pipewire-0 default.clock.rate = 16000 }
context.spa-libs = { audio.convert.* = audioconvert/libspa-audioconvert support.* = support/libspa-support }
context.modules = [
 { name = libpipewire-module-protocol-native }
 { name = libpipewire-module-access args = { access.socket = { pipewire-0 = "unrestricted" pipewire-0-manager = "unrestricted" } } }
 { name = libpipewire-module-metadata }
 { name = libpipewire-module-spa-node-factory }
 { name = libpipewire-module-client-node }
 { name = libpipewire-module-adapter }
 { name = libpipewire-module-link-factory }
]
context.objects = [
 { factory = spa-node-factory args = { factory.name = support.node.driver node.name = qa-driver priority.driver = 20000 } }
 { factory = adapter args = { factory.name = support.null-audio-sink node.name = hv-qa-silence media.class = Audio/Source/Virtual node.virtual = true audio.rate = 16000 audio.channels = 1 audio.position = [ MONO ] adapter.auto-port-config = { mode = dsp monitor = true position = preserve } } }
]
''')
    env['PIPEWIRE_REMOTE'] = str(runtime / 'pipewire-0')
    env['PIPEWIRE_RUNTIME_DIR'] = str(runtime)
    spawn(['/usr/bin/pipewire', '-c', str(pw_config)], 'pipewire')
    wait(lambda: (runtime / 'pipewire-0').is_socket())
    sources = [n['info']['props']['node.name'] for n in json.loads(run('pw-dump'))
               if n['type'] == 'PipeWire:Interface:Node' and
               n['info']['props'].get('media.class', '').startswith('Audio/Source')]
    check('only-private-zero-source', sources == ['hv-qa-silence'])
    models = runtime / 'models'
    models.mkdir()
    for name in ('funasr-encoder-f16.gguf', 'qwen3-0.6b-q8_0.gguf', 'fsmn-vad.gguf'):
        (models / name).touch()
    transcript_plan, decode_count = runtime / 'transcripts.json', runtime / 'decode-count'
    worker = runtime / 'synthetic-worker'
    worker.write_text('''#!/usr/bin/python3
import json, os, pathlib, sys
root = pathlib.Path(os.environ['XDG_RUNTIME_DIR'])
print('{"ready":true}', flush=True)
for line in sys.stdin:
    audio = pathlib.Path(json.loads(line)['audio'])
    assert audio.parent.resolve() == pathlib.Path(os.environ['TMPDIR']).resolve()
    wave = audio.read_bytes()
    assert wave[:4] == b'RIFF' and wave[36:40] == b'data'
    assert len(wave) > 44 and not any(wave[44:]), 'Only zero samples are allowed'
    counter = root/'decode-count'
    index = int(counter.read_text())
    text = json.loads((root/'transcripts.json').read_text())[index]
    counter.write_text(str(index + 1))
    print(json.dumps({'text':text, 'speech':True}, ensure_ascii=False), flush=True)
''')
    worker.chmod(0o700)
    server = Server(('127.0.0.1', 0), Handler)
    server.lock, server.bodies, server.plan = threading.Lock(), [], collections.deque()
    server.unexpected, server.output, server.returned = 0, '', threading.Event()
    threading.Thread(target=lambda: server.serve_forever(poll_interval=.02), daemon=True).start()
    env.update(HYPRVOICE_RETRY_TEST_KEY='synthetic-retry-key',
               NO_PROXY='127.0.0.1,localhost', no_proxy='127.0.0.1,localhost')
    config = {'asr': {'backend': 'fun'}, 'fun': {'worker': str(worker), 'model_dir': str(models)},
              'scene': 'correct', 'context': {'enabled': True}, 'audio_source': 'hv-qa-silence',
              'llm': {'model': 'local-test-double', 'allow_http': True,
                      'api_key_env': 'HYPRVOICE_RETRY_TEST_KEY',
                      'base_url': f'http://127.0.0.1:{server.server_port}'}}
    config_path = runtime / 'config.json'
    env['HYPRVOICE_CONFIG'] = str(config_path)
    keyboard = spawn([str(repo / 'build/test_keyboard')], 'keyboard')
    wait(lambda: 'READY' in (evidence / 'keyboard.log').read_text())
    # A brand-new home/profile and local file page; all browser HTTP(S) is
    # directed to an unreachable loopback proxy. No daily browser state is used.
    browser_home = runtime/'browser-home'
    browser_home.mkdir(mode=0o700)
    profile = runtime/'browser-profile'
    profile.mkdir(mode=0o700)
    env['HOME'] = str(browser_home)
    page = runtime/'browser-target.html'
    page.write_text(r"""<!doctype html><meta charset="utf-8"><title>HV-QA loading</title>
<style>body{font:16px sans-serif} #history{height:220px;overflow:auto}textarea,[contenteditable]{display:block;border:1px solid #888;min-height:45px;margin:10px;width:650px}p{margin:0}</style>
<div id="history"></div>
<form><textarea id="textarea" aria-label="Public textarea" autofocus></textarea>
<input id="password" aria-label="Public password" type="password" value="public-test-password">
<div id="contenteditable" aria-label="Public chat editor" contenteditable="true" role="textbox" tabindex="0"><p><br></p></div>
<div id="other" aria-label="Public second editor" contenteditable="true" role="textbox" tabindex="0"><p><br></p></div>
<input id="readonly" aria-label="Public readonly" readonly value="public-readonly">
<button type="submit">Public send</button></form>
<script>
let counts={},pastes={},sent=0;
const rich=e=>[...e.childNodes].every(n=>n.nodeType===1&&['P','DIV'].includes(n.tagName))?[...e.children].map(n=>n.children.length===1&&n.firstChild.tagName==='BR'?'':n.innerText).join('\n'):e.innerText.replace(/\n+$/,'');
const value=e=>e.id==='password'?'[protected]':(e.isContentEditable?rich(e):e.value||'');
const title=()=>{let e=document.activeElement;document.title='HV-QA '+e.id+' '+(counts[e.id]||0)+' '+sent+' '+encodeURIComponent(JSON.stringify({text:value(e),paste:pastes[e.id]||null}));};
document.querySelectorAll('textarea,input,[contenteditable]').forEach(e=>{e.addEventListener('focus',title);e.addEventListener('paste',ev=>{if(e.id!=='password')pastes[e.id]=ev.clipboardData.getData('text/plain')});e.addEventListener('input',()=>{counts[e.id]=(counts[e.id]||0)+1;title()})});
document.querySelector('form').addEventListener('submit',e=>{e.preventDefault();sent++;title()});
for(let i=0;i<1200;i++){let p=document.createElement('p');p.textContent='Public synthetic chat history '+i;document.getElementById('history').append(p)}
function choose(id,start=false){let e=document.getElementById(id);e.focus();if(e.isContentEditable){let r=document.createRange();r.selectNodeContents(e);r.collapse(start);let s=window.getSelection();s.removeAllRanges();s.addRange(r)}else if(e.setSelectionRange){try{e.setSelectionRange(start?0:e.value.length,start?0:e.value.length)}catch(_){}}title()}
document.addEventListener('keydown',e=>{let ids={F2:'textarea',F3:'password',F4:'contenteditable',F5:'other',F6:'readonly'};
if(ids[e.key]){e.preventDefault();choose(ids[e.key]);return}
if(e.key==='F1'){e.preventDefault();['textarea','contenteditable','other'].forEach(id=>{let x=document.getElementById(id);if(x.isContentEditable)x.innerHTML='<p><br></p>';else x.value='';counts[id]=0;pastes[id]=null});choose('textarea');return}
if(e.key==='F7'){e.preventDefault();choose('contenteditable',true);return}
if(e.key==='F10'){e.preventDefault();document.getElementById('contenteditable').innerHTML='<p>Public first line 中文🙂.</p><p>Public second line.</p>';choose('contenteditable');return}
if(e.key==='F8'){e.preventDefault();document.querySelector('#history p').textContent='Changed public background history';title();return}
if(e.key==='F12'){e.preventDefault();document.getElementById('textarea').value='公开选区🙂。\nPublic second line.';choose('textarea');return}
if(e.key==='F11'){e.preventDefault();let x=document.getElementById('contenteditable');x.innerHTML=x.innerHTML.replace('Public first','PUBLIC FIRST');choose('contenteditable');return}});
choose('textarea');
</script>""")
    browser_extra = {}
    if env.get('HYPRVOICE_WINDOW_QA'):
        browser_extra = {'DBUS_SESSION_BUS_ADDRESS':'unix:path='+str(runtime/'browser-session'),
                         'AT_SPI_BUS_ADDRESS':'unix:path='+str(runtime/'browser-a11y')}
        for name in ('browser-session','browser-a11y'):
            spawn(['/usr/bin/dbus-daemon','--session','--nofork','--nopidfile',
                   '--address=unix:path='+str(runtime/name)], name)
            wait(lambda:(runtime/name).is_socket())
    if env.get('HYPRVOICE_WINDOW_QA') == 'electron':
        electron_main=runtime/'electron-main.js'
        electron_main.write_text("""const {app,BrowserWindow,session}=require('electron');
app.setPath('userData',process.argv[2]);
app.whenReady().then(()=>{
 session.defaultSession.webRequest.onBeforeRequest({urls:['http://*/*','https://*/*']},(d,cb)=>cb({cancel:true}));
 const win=new BrowserWindow({width:1000,height:680,webPreferences:{sandbox:true,nodeIntegration:false,contextIsolation:true}});
 win.loadFile(process.argv[3]);
});app.on('window-all-closed',()=>app.quit());""")
        browser=spawn(['/usr/lib/electron42/electron',str(electron_main),str(profile),str(page),
                       '--ozone-platform=x11','--disable-background-networking',
                       '--proxy-server=http://127.0.0.1:9','--proxy-bypass-list=<-loopback>',
                       '--host-resolver-rules=MAP * ~NOTFOUND'],'browser',browser_extra)
    else:
        browser = spawn(['/usr/bin/google-chrome-stable', '--user-data-dir='+str(profile),
                     '--ozone-platform=wayland', '--no-first-run', '--no-default-browser-check',
                     '--disable-background-networking', '--disable-sync', '--disable-extensions',
                     '--disable-component-update', '--disable-domain-reliability', '--disable-dev-shm-usage',
                     '--proxy-server=http://127.0.0.1:9', '--proxy-bypass-list=<-loopback>',
                     '--host-resolver-rules=MAP * ~NOTFOUND',
                     page.as_uri()], 'browser', browser_extra)
    def browser_client():
        return next((c for c in json.loads(run('hyprctl','clients','-j')) if c['pid']==browser.pid),None)
    wait(browser_client,seconds=25)
    focus(browser)
    time.sleep(1)
    cases=repo/'tests'/('window_input_cases.py' if env.get('HYPRVOICE_WINDOW_QA') else 'browser_input_cases.py')
    exec(compile(cases.read_text(), str(cases), 'exec'), globals())
    # A terminal runs a fixed Python program, never a shell or user history.
    terminal_program=runtime/'terminal_receiver.py'
    terminal_ready=runtime/'terminal-ready'
    terminal_input=runtime/'terminal-input'
    terminal_program.write_text("import os,pathlib,sys,termios,tty\np=pathlib.Path(sys.argv[1]);p.write_bytes(b'')\ntty.setraw(sys.stdin.fileno())\nprint('Public synthetic terminal receiver',flush=True)\npathlib.Path(sys.argv[2]).write_text(str(os.getpid()))\nwhile True:\n b=os.read(sys.stdin.fileno(),4096)\n if not b: break\n with p.open('ab') as f: f.write(b)\n")
    if env.get('HYPRVOICE_GHOSTTY_QA'):
        terminal=spawn(['/usr/bin/ghostty','--gtk-single-instance=false',
                        '--shell-integration=none','--confirm-close-surface=false',
                        '--working-directory='+str(runtime),'-e',
                        '/usr/bin/python3','-I','-u',str(terminal_program),str(terminal_input),str(terminal_ready)],'terminal')
    else:
        terminal=spawn(['/usr/bin/kitty','--config','NONE','--directory',str(runtime),
                        '--class','kitty','--title','HV-QA Terminal Receiver',
                        '/usr/bin/python3','-I','-u',str(terminal_program),str(terminal_input),str(terminal_ready)],'terminal')
    wait(lambda:terminal_ready.is_file(),seconds=15)
    wait(lambda:any(c['pid']==terminal.pid for c in json.loads(run('hyprctl','clients','-j'))))
    focus(terminal)
    guard=json.loads(run(str(candidate),'read-target',str(terminal.pid)))
    (evidence/'target-terminal.json').write_text(json.dumps(guard,indent=2))
    check('terminal-fixed-program-no-initial-input',terminal_input.read_bytes()==b'')
    check('terminal-target-not-claimed-reliable',not guard['reliable'] and 'text' not in guard)
    check('terminal-command-rejected-before-copy',command('command')[0]==1 and terminal_input.read_bytes()==b'')
    boot(auto=True)
    raw='Public synthetic terminal dictation. 中文🙂'
    set_transcripts(raw); index=len(server.bodies)
    begin(); complete=finish('idle')
    wait(lambda: terminal_input.read_text()==raw)
    check('terminal-automatic-input-without-editor-metadata',terminal_input.read_text()==raw)
    check('terminal-no-http-no-context',len(server.bodies)==index and complete['context']=='')
    check('terminal-result-consumed-exactly-once',command('commit')[0]==1 and command('insert-current')[0]==1)
    boot(auto=False)
    manual=' Manual public text.'
    set_transcripts(manual);begin();complete=finish()
    label('输入');click('输入');phase('idle')
    wait(lambda:terminal_input.read_text()==raw+manual)
    check('terminal-one-click-input-without-copy',command('insert-current')[0]==1 and terminal_input.read_text()==raw+manual)
    result['terminal_version']=run('/usr/bin/ghostty','+version').strip() if env.get('HYPRVOICE_GHOSTTY_QA') else run('/usr/bin/kitty','--version').strip()
    check('no-unplanned-http-requests',not server.plan and server.unexpected==0)
    result['http_requests']=len(server.bodies)
    result['browser_version']=(subprocess.check_output(['/usr/lib/electron42/electron','-e','process.stdout.write(process.versions.electron)'],env=dict(env,ELECTRON_RUN_AS_NODE='1'),text=True).strip() if env.get('HYPRVOICE_WINDOW_QA')=='electron' else run('/usr/bin/google-chrome-stable','--version').strip())
    result['checks_passed']=len(checks)
finally:
    if server:
        server.shutdown()
        server.server_close()
    for p, log in reversed(children):
        if p.poll() is None:
            p.terminate()
            try:
                p.wait(timeout=6)
            except subprocess.TimeoutExpired:
                p.kill(); p.wait(timeout=2)
        log.close()
    result['checks_passed']=len(checks)
    result['all_test_children_stopped']=all(p.poll() is not None for p,_ in children)
    (evidence/'checks-result.json').write_text(json.dumps(result,ensure_ascii=False,indent=2))
    print(json.dumps(result,ensure_ascii=False,indent=2))
