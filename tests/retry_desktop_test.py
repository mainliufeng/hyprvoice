#!/usr/bin/env python3
"""Production App retry/commit acceptance, private GTK + zero-source PW + HTTP.

Requires an empty, explicitly isolated Hyprland/AT-SPI session and existing
build targets. Refuses the daily runtime; all text/audio here is synthetic.
"""
import collections
import hashlib
import http.server
import json
import os
import subprocess
import sys
import threading
import time
from pathlib import Path

repo = Path(__file__).resolve().parents[1]
runtime, evidence = map(Path, sys.argv[1:])
env = dict(os.environ)
assert runtime.resolve() != Path(f'/run/user/{os.getuid()}')
assert env['XDG_RUNTIME_DIR'] == str(runtime)
assert env['AT_SPI_BUS_ADDRESS'].startswith('unix:path=' + str(runtime) + '/')
evidence.mkdir(exist_ok=True)
children, checks = [], []
result = {'checks': checks, 'real_microphone': False, 'external_model_calls': False}
app = None
server = None


def spawn(argv, name):
    log = (evidence / (name + '.log')).open('w')
    p = subprocess.Popen(argv, env=env, stdin=subprocess.PIPE, stdout=log,
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
    r = subprocess.run([str(repo / 'build/hyprvoice'), *argv], env=env,
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
    return json.loads(run(*args))


def label(name):
    def visible():
        value = accessible()
        return value if any(w['name'] == name and w['showing']
                            for w in value['widgets']) else None
    return wait(visible)


def click(name):
    label(name)
    assert accessible(name)['activated']


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


def editor(text, name, password=False):
    path = runtime / (name + '.txt')
    p = spawn([str(repo / 'build/test_editor'), str(path), text] +
              (['password'] if password else []), name)
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
    app = spawn([str(repo / 'build/hyprvoice'), 'serve'], 'app-' + str(len(children)))
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
    assert command(mode)[0] == 0
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
    first, first_path = editor('Public synthetic history. ', 'first-editor')
    raw = '合成原文：不会支付2400元'
    processed = '合成结果：不会支付2400元。'

    # Default auto_commit must not apply to a retry. Change actual editor history
    # after capture to verify that the retry never re-reads it.
    boot(restore=True)
    set_transcripts(raw)
    gate = threading.Event()
    index = plan('failure', gate, output=processed)
    begin()
    original = finish()
    check('initial-failure-retains-raw-and-retry', original['raw'] == raw and
          original['text'] == raw and original['retry_available'] and original['error'])
    check('exact-synthetic-context-captured', original['context'] == first_path.read_text())
    label('使用原文')
    key('type changed')
    changed = first_path.read_text()
    click('重试文字处理')
    phase('retrying')
    label('正在重试文字处理 · 纠错')
    widgets = label('重试中…')['widgets']
    check('retry-button-disabled-inflight', any(w['name'] == '重试中…' and
          not w['sensitive'] for w in widgets))
    photo('retry-in-progress')
    check('duplicate-retry-rejected', command('retry')[0] == 1)
    check('new-recording-rejected-during-retry', command('start')[0] == 1)
    check('commit-rejected-during-retry', command('commit')[0] == 1)
    wait(lambda: len(server.bodies) == index + 2)
    check('single-inflight-retry-request', len(server.bodies) == index + 2)
    check('retry-request-byte-identical', server.bodies[index] == server.bodies[index + 1])
    check('retry-never-reruns-asr', decode_count.read_text() == '1')
    gate.set()
    complete = phase('ready')
    check('retry-success-needs-manual-confirmation', complete['text'] == processed and
          complete['manual_confirmation'] and not complete['error'])
    time.sleep(.3)
    check('retry-never-auto-inserts', first_path.read_text() == changed)
    label('重试结果需确认后插入')
    photo('retry-ready')
    other, other_path = editor('Public unrelated editor.', 'other-editor')
    click('插入文字')
    wait(lambda: '目标窗口已变化' in state()['error'])
    check('wrong-window-commit-rejected', state()['phase'] == 'ready' and
          first_path.read_text() == changed and other_path.read_text() == 'Public unrelated editor.')
    focus(first)
    # Fail only the optional clipboard restore, after the genuine paste shortcut.
    run('/usr/bin/wl-copy', 'Public synthetic previous clipboard')
    bin_dir = runtime / 'bin'
    bin_dir.mkdir()
    restore_count = runtime / 'restore-count'
    restore_count.write_text('0')
    wrapper = bin_dir / 'wl-copy'
    wrapper.write_text('''#!/usr/bin/python3
import os, pathlib, sys
p=pathlib.Path(os.environ['XDG_RUNTIME_DIR'])/'restore-count'
n=int(p.read_text())+1
p.write_text(str(n))
if n==2: sys.exit(1)
os.execv('/usr/bin/wl-copy', ['/usr/bin/wl-copy', *sys.argv[1:]])
''')
    wrapper.chmod(0o700)
    # PATH is inherited at app launch, so this wrapper is enabled in the next
    # dedicated restore fixture; this first commit exercises genuine restore.
    click('插入文字')
    phase('idle')
    wait(lambda: first_path.read_text() == changed + processed)
    check('real-editor-inserted-exactly-once', first_path.read_text() == changed + processed)
    check('duplicate-commit-rejected', command('commit')[0] == 1)
    check('committed-result-cannot-retry', command('retry')[0] == 1)
    check('duplicate-commit-does-not-paste', first_path.read_text() == changed + processed)

    # The app receives a failing wl-copy restore in this private launch only.
    original_path = env['PATH']
    env['PATH'] = str(bin_dir) + ':' + original_path
    boot(restore=True)
    set_transcripts(raw)
    index = plan('failure', 'success', output=processed)
    begin(); finish()
    assert command('retry')[0] == 0
    phase('ready')
    run('/usr/bin/wl-copy', 'Public synthetic restore fixture')
    before = first_path.read_text()
    assert command('commit')[0] == 0
    phase('idle')
    wait(lambda: first_path.read_text() == before + processed)
    check('restore-error-does-not-reenable-commit', restore_count.read_text() == '2' and
          command('commit')[0] == 1 and first_path.read_text() == before + processed)
    env['PATH'] = original_path

    # Empty and timed-out retries retain raw for explicit use, never autopaste.
    boot()
    set_transcripts(raw)
    index = plan('failure', 'empty')
    begin(); finish()
    click('重试文字处理')
    failed = phase('ready')
    check('empty-retry-keeps-original', failed['text'] == raw and failed['raw'] == raw and failed['error'])
    label('使用原文')
    before = first_path.read_text()
    click('使用原文')
    phase('idle')
    wait(lambda: first_path.read_text() == before + raw)
    check('raw-fallback-inserted-once', first_path.read_text() == before + raw and command('raw')[0] == 1)
    boot(timeout=1)
    set_transcripts(raw)
    index = plan('failure', 'timeout')
    begin(); finish()
    assert command('retry')[0] == 0
    timed = phase('ready')
    check('timeout-retry-keeps-original', timed['text'] == raw and timed['raw'] == raw and timed['error'])
    assert command('cancel')[0] == 0
    phase('idle')

    # Release a late HTTP response while a fresh recording is already active.
    boot()
    next_raw = '合成下一轮：不重启服务'
    set_transcripts(raw, next_raw)
    gate = threading.Event()
    index = plan('failure', gate, 'failure')
    begin(); finish()
    click('重试文字处理')
    phase('retrying')
    wait(lambda: len(server.bodies) == index + 2)
    click('取消本次语音')
    wait(lambda: not state()['retry_available'])
    cancelling = state()
    check('cancel-immediately-clears-private-snapshot', not cancelling['retry_available'] and
          cancelling['raw'] == '' and cancelling['context'] == '')
    if cancelling['busy']:
        check('new-recording-rejected-until-cancel-drains', command('start')[0] == 1)
    phase('idle')
    check('cancelled-result-cannot-retry', command('retry')[0] == 1)
    begin()
    gate.set()
    wait(lambda: server.returned.is_set())
    time.sleep(.2)
    fresh = state()
    check('late-response-cannot-pollute-next-recording', fresh['phase'] == 'recording' and
          fresh['raw'] == '' and fresh['text'] == '' and not fresh['retry_available'])
    fresh = finish()
    check('next-recording-has-only-new-transcript', fresh['raw'] == next_raw and fresh['text'] == next_raw)
    check('cancel-retry-did-not-decode-again', decode_count.read_text() == '2')
    assert command('cancel')[0] == 0
    phase('idle')

    # Command retry keeps the originally selected text, even after the live
    # selection changes. Submission still checks the original selection.
    boot()
    focus(first)
    key('ctrl a'); key('type public selected budget 2400.'); key('ctrl a')
    selected = first_path.read_text()
    instruction, replacement = '合成指令：金额改为3000', 'Public selected budget 3000.'
    set_transcripts(instruction)
    index = plan('failure', 'success', output=replacement)
    begin('command')
    failed = finish('error')
    check('command-failure-never-offers-spoken-instruction', failed['text'] == '' and
          failed['raw'] == instruction and failed['retry_available'] and command('raw')[0] == 1)
    widgets = accessible()['widgets']
    check('command-has-no-raw-button', not any(w['name'] == '使用原文' and w['showing'] for w in widgets))
    key('key Left')
    click('重试文字处理')
    ready = phase('ready')
    check('command-retry-request-byte-identical', server.bodies[index] == server.bodies[index + 1])
    check('command-retry-success-is-replacement', ready['text'] == replacement and ready['manual_confirmation'])
    label('重试结果需确认后替换选中文字')
    check('changed-selection-commit-rejected', command('commit')[0] == 1 and first_path.read_text() == selected)
    key('ctrl a')
    click('确认修改')
    phase('idle')
    wait(lambda: first_path.read_text() == replacement)
    check('command-replaces-selection-exactly-once', first_path.read_text() == replacement and command('commit')[0] == 1)

    # Protected input never creates a retryable cloud request.
    boot(auto=False)
    secret, secret_path = editor('public-test-password', 'password-editor', password=True)
    set_transcripts(raw)
    index = len(server.bodies)
    begin()
    protected = finish()
    check('protected-input-context-remains-empty', protected['context'] == '' and '密码' in protected['context_note'])
    check('protected-input-never-retries-or-calls-http', not protected['retry_available'] and
          command('retry')[0] == 1 and len(server.bodies) == index)
    assert command('cancel')[0] == 0
    phase('idle')
    check('no-unplanned-http-requests', server.unexpected == 0)
    result['http_requests'] = len(server.bodies)
    result['request_sha256'] = [hashlib.sha256(b).hexdigest() for b in server.bodies]
    result['checks_passed'] = len(checks)
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
    result['checks_passed'] = len(checks)
    result['all_test_children_stopped'] = all(p.poll() is not None for p, _ in children)
    (evidence / 'checks-result.json').write_text(json.dumps(result, ensure_ascii=False, indent=2))
    print(json.dumps(result, ensure_ascii=False, indent=2))
