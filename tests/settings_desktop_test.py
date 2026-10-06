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
import signal
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


def keyboard_focus(name):
    for _ in range(18):
        values = accessible()['widgets']
        if any(w['name'] == name and w.get('focus_within') for w in values):
            return
        key('key Tab')
    (evidence/'focus-failure.json').write_text(json.dumps(accessible(), ensure_ascii=False, indent=2))
    photo('focus-failure')
    raise RuntimeError('Keyboard cannot reach named settings control: '+name)


def widget(name):
    return next(w for w in label(name)['widgets'] if w['name'] == name and w['showing'])


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
    started = time.monotonic()
    assert command(mode)[0] == 0
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
    first, first_path = editor('Public synthetic history. ', 'first-editor')
    (evidence/'initial-target.json').write_text(run(str(repo/'build/hyprvoice'), 'read-target', str(first.pid)))
    raw = '合成原文：不会支付2400元'
    processed = '合成结果：不会支付2400元。'

    # Settings acceptance uses the production App, private widgets and zero audio.
    config['prompts'] = {'custom <b>&': 'Public synthetic settings prompt'}
    config['llm']['legacy_key'] = 'private-settings-canary'
    config['private_other'] = 'private-settings-canary'
    boot(auto=False)
    original = config_path.read_text()
    initial = command('settings-status')[1]['settings']
    assert command('settings')[0] == 0
    label('Hyprvoice 设置与诊断')
    check('settings-have-context-consent-text', bool(label('前文辅助：会发送光标前已有文字')))
    check('recording-blocked-while-settings-hold-focus', command('start')[0] == 1 and not state()['busy'])
    # Toggle a draft using the real keyboard; Escape discards it.
    keyboard_focus('完成后自动插入（位置核验通过时）')
    key('key space')
    key('key Escape')
    wait(lambda: not command('settings-status')[1]['open'])
    check('escape-discards-keyboard-draft', config_path.read_text() == original and
          command('settings-status')[1]['settings'] == initial)
    assert command('settings')[0] == 0
    label('Hyprvoice 设置与诊断')
    check('no-previous-before-first-save', command('settings-previous')[0] == 1)
    for patch in ({'auto_commit': 'private-settings-canary'}, {'context': {'max_chars': 2048}},
                  {'asr': {'backend': 'private-settings-canary'}}, {'llm': {'api_key': 'private-settings-canary'}}):
        code, reply = command('settings-save', json.dumps(patch))
        check('invalid-patch-does-not-leak-or-save-' + str(len(checks)), code == 1 and
              'private-settings-canary' not in json.dumps(reply) and config_path.read_text() == original)
    code, reply = command('settings-save', '{"key":"private-settings-canary"')
    check('malformed-json-is-redacted', code == 1 and 'private-settings-canary' not in json.dumps(reply))
    # A real drop-down exposes built-in and custom scenes as plain labels.
    keyboard_focus('文字场景')
    key('key space')
    custom = label('custom <b>&')
    check('custom-scene-listed-as-literal-text', bool(custom))
    # GTK selects a row with End/Return. Use Home/Down to choose the custom row
    # (raw, correct, custom, format, translate in sorted prompt order).
    key('key Home'); key('key Down'); key('key Down'); key('key Return')
    keyboard_focus('前文辅助：会发送光标前已有文字')
    key('key space')
    keyboard_focus('完成后自动插入（位置核验通过时）')
    key('key space')
    keyboard_focus('保存设置')
    key('key Return')
    wait(lambda: not command('settings-status')[1]['saving'])
    saved = command('settings-status')[1]['settings']
    check('keyboard-save-persists-and-applies-four-settings', saved['scene'] == 'custom <b>&' and
          saved['auto_commit'] and not saved['context']['enabled'] and saved['asr']['backend'] == 'fun' and
          json.loads(config_path.read_text())['scene'] == 'custom <b>&')
    stored = json.loads(config_path.read_text())
    check('save-preserves-unexposed-values', stored['llm'] == config['llm'] and
          stored['audio_source'] == 'hv-qa-silence' and stored['private_other'] == 'private-settings-canary')
    check('restore-is-only-four-values-in-memory', command('settings-previous')[1]['settings'] == initial)
    click('恢复上次设置')
    check('restore-fills-draft-without-changing-live-config', command('settings-status')[1]['settings'] == saved)
    label('上次设置已填回草稿；点击保存才会生效')
    click('保存设置')
    wait(lambda: (s := command('settings-status')[1])['settings'] == initial and not s['saving'])
    check('restore-save-reapplies-previous-settings', command('settings-status')[1]['settings'] == initial)
    stable = config_path.read_text()
    assert command('settings-save', json.dumps({'asr': {'backend': 'x-asr'}}))[0] == 0
    wait(lambda: not command('settings-status')[1]['saving'])
    check('failed-new-backend-preserves-loaded-old-and-file', config_path.read_text() == stable and
          state()['backend'] == 'fun' and state()['model_ready'])
    assert command('scene', 'custom <b>&')[0] == 0
    assert command('backend', 'x-asr')[0] == 0
    wait(lambda: not command('settings-status')[1]['saving'])
    check('legacy-backend-failure-preserves-runtime-scene-and-disk',
          state()['scene'] == 'custom <b>&' and state()['backend'] == 'fun' and
          config_path.read_text() == stable)
    assert command('scene', 'correct')[0] == 0
    before_http, before_decode = len(server.bodies), decode_count.read_text() if decode_count.exists() else None
    click('刷新本地诊断（不录音／不联网）')
    label('本地识别：曾完成当前后端初始化；未做本次识别测试\n模型文件／执行程序：已发现（不代表识别成功）\n文本服务：配置齐全，未联网验证\n录音来源：hv-qa-silence\n音源图：发现配置来源；尚未试麦\n桌面依赖：已发现\n控制目录：当前用户私有\n配置文件：与当前已加载版本一致')
    diagnostic = command('diagnose')[1]['diagnostics']
    widgets = accessible()['widgets']
    check('diagnosis-does-not-capture-or-call-model', len(server.bodies) == before_http and
          (decode_count.read_text() if decode_count.exists() else None) == before_decode and
          not diagnostic['audio_tested'] and not diagnostic['network_tested'])
    check('diagnosis-reports-source-and-init-honestly', diagnostic['recognizer_loaded'] and
          diagnostic['configured_source_present'] and diagnostic['assets_present'])
    check('no-private-canary-in-ui-or-ipc', 'private-settings-canary' not in
          json.dumps([widgets, command('settings-status')[1], command('diagnose')[1]]))
    photo('settings-diagnostics')
    # Independent on-disk edits are never replaced by the cached settings draft.
    external = stable + '\n'
    config_path.write_text(external)
    assert command('settings-save', json.dumps({'auto_commit': True}))[0] == 0
    wait(lambda: not command('settings-status')[1]['saving'])
    check('external-edit-preserved-and-loaded-settings-retained', config_path.read_text() == external and
          command('settings-status')[1]['settings'] == initial)
    check('external-config-diagnosis-stale', not command('diagnose')[1]['diagnostics']['config_current'])
    config_path.write_text(stable)
    click('取消／关闭')
    wait(lambda: not command('settings-status')[1]['open'])
    focus(first)
    set_transcripts(raw)
    index = plan('failure')
    begin()
    recording = state()
    check('recording-rejects-open-save-and-diagnosis', command('settings')[0] == 1 and
          command('settings-save', json.dumps({'scene': 'raw'}))[0] == 1 and command('diagnose')[0] == 1 and
          state()['phase'] == 'recording' and config_path.read_text() == stable)
    pending = finish()
    check('pending-failure-keeps-retry-and-raw', pending['raw'] == raw and pending['retry_available'])
    check('pending-result-rejects-settings-without-loss', command('settings')[0] == 1 and
          command('settings-save', json.dumps({'auto_commit': True}))[0] == 1 and
          state()['raw'] == pending['raw'] and state()['text'] == pending['text'] and
          state()['error'] == pending['error'] and state()['retry_available'])
    recovery_gate = threading.Event()
    plan(recovery_gate, output=processed)
    assert command('retry')[0] == 0
    phase('retrying')
    check('busy-retry-rejects-setting-changes-without-cancel', command('settings')[0] == 1 and
          command('settings-save', json.dumps({'scene': 'raw'}))[0] == 1 and
          state()['phase'] == 'retrying' and state()['retry_available'])
    recovery_gate.set()
    phase('ready')
    check('session-still-retries-after-settings-rejected', state()['text'] == processed and state()['retry_available'])
    assert command('cancel')[0] == 0
    phase('idle')
    # A stalled new model load can be cancelled without committing the candidate.
    assert command('quit')[0] == 0
    app.wait(timeout=6)
    # Retry a previously failed initialization with a worker reporting an error,
    # then replace only the synthetic worker with a delayed one for save cancel.
    worker.write_text('#!/usr/bin/python3\nprint("{\\\"error\\\":\\\"private-settings-canary\\\"}", flush=True)\n')
    app = spawn([str(repo/'build/hyprvoice'), 'serve'], 'failed-init-app')
    wait(lambda: (runtime/'hyprvoice/control.sock').is_socket())
    phase('error')
    worker.write_text('#!/usr/bin/python3\nimport time\ntime.sleep(30)\n')
    assert command('settings')[0] == 0
    assert command('settings-save', json.dumps({'asr': {'backend': 'fun'}}))[0] == 0
    wait(lambda: command('settings-status')[1]['saving'])
    label('取消保存')
    check('save-disables-dangerous-controls', not widget('保存设置')['sensitive'] and
          not widget('恢复上次设置')['sensitive'])
    key('key Escape')
    wait(lambda: not command('settings-status')[1]['saving'], seconds=3)
    check('cancelled-model-load-keeps-file-and-unready-truth', config_path.read_text() == stable and
          not command('diagnose')[1]['diagnostics']['recognizer_loaded'] and
          '取消' in command('settings-status')[1]['message'])
    check('settings-test-no-unplanned-network', server.unexpected == 0)
    result['http_requests'] = len(server.bodies)
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
