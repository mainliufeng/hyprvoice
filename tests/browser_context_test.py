#!/usr/bin/env python3
"""Real Chrome context checks; requires an isolated Hyprland/AT-SPI session."""
import json
import os
import subprocess
import sys
import time
from pathlib import Path

root = Path(__file__).resolve().parents[1]
if os.environ.get('XDG_RUNTIME_DIR') == f'/run/user/{os.getuid()}':
    raise SystemExit('Refusing daily desktop')
out = Path(sys.argv[1]).resolve()
out.mkdir(parents=True, exist_ok=True)
env = dict(os.environ, GTK_IM_MODULE='none', XDG_CONFIG_HOME=str(out/'config'))
Path(env['XDG_CONFIG_HOME']).mkdir(exist_ok=True)
page = out/'context.html'
page.write_text('<!doctype html><meta charset="utf-8"><title>Hyprvoice browser context QA</title>'
                '<h1>Public context test</h1><textarea autofocus>浏览器中的 Hyprvoice 前文。</textarea>'
                '<input type="password" value="public-test-password">'
                '<div contenteditable="true" role="textbox" tabindex="0">网页编辑区的前文。</div>')
checks = []
keyboard = subprocess.Popen([str(root/'build/test_keyboard')], env=env, stdin=subprocess.PIPE,
                            stdout=subprocess.PIPE, text=True)
assert keyboard.stdout.readline().strip() == 'READY'


def key(s):
    keyboard.stdin.write(s+'\n')
    keyboard.stdin.flush()
    assert keyboard.stdout.readline().strip() == 'OK'
    time.sleep(.2)


def read(pid):
    return json.loads(subprocess.check_output([str(root/'build/hyprvoice'), 'read-context', str(pid), '1024'],
                                             env=env, text=True, timeout=4))


def check(name, condition):
    assert condition, name
    checks.append(name)
    print('PASS', name, flush=True)


try:
    baseline = None
    for enabled in [False, True]:
        if enabled:
            (Path(env['XDG_CONFIG_HOME'])/'chrome-flags.conf').write_text('--force-renderer-accessibility=complete\n')
        browser = subprocess.Popen(['/usr/bin/google-chrome-stable', '--user-data-dir='+str(out/f'profile-{enabled}'),
                                    '--ozone-platform=wayland', '--no-first-run', '--no-default-browser-check',
                                    '--disable-background-networking', '--disable-sync', '--disable-extensions',
                                    '--disable-dev-shm-usage', page.as_uri()], env=env,
                                   stdout=(out/f'browser-{enabled}.log').open('w'), stderr=subprocess.STDOUT)
        try:
            deadline = time.monotonic()+20
            while time.monotonic() < deadline:
                clients = json.loads(subprocess.check_output(['hyprctl', 'clients', '-j'], env=env, text=True))
                if any(c['pid'] == browser.pid for c in clients):
                    break
                time.sleep(.2)
            else:
                raise RuntimeError('Test Chrome failed to show a window')
            subprocess.run(['hyprctl', 'dispatch', 'focuswindow', f'pid:{browser.pid}'], env=env,
                           check=True, capture_output=True)
            time.sleep(2)
            key('ctrl End')
            if not enabled:
                baseline = read(browser.pid)['available']
                continue
            check('launcher loads accessibility flag and reads textarea', read(browser.pid)['text'] == '浏览器中的 Hyprvoice 前文。')
            key('ctrl Home')
            check('Chrome text after caret is excluded', read(browser.pid)['text'] == '')
            key('ctrl End')
            key('type next')
            check('Chrome keyboard edits update context', read(browser.pid)['text'] == '浏览器中的 Hyprvoice 前文。next')
            key('key Tab')
            value = read(browser.pid)
            check('Chrome password value is never read', value['protected'] and not value['available'] and value['text'] == '')
            key('key Tab')
            key('ctrl End')
            check('Chrome contenteditable context is read', read(browser.pid)['text'] == '网页编辑区的前文。')
        finally:
            browser.terminate()
            browser.wait(timeout=10)
    report = {'checks': checks, 'default_chrome_context_available': baseline,
              'version': subprocess.check_output(['/usr/bin/google-chrome-stable', '--version'], text=True).strip()}
    (out/'result.json').write_text(json.dumps(report, ensure_ascii=False, indent=2))
finally:
    keyboard.terminate()
    keyboard.wait(timeout=5)
