#!/usr/bin/env python3
"""Read real GTK input context in an isolated Hyprland/AT-SPI session."""
import json
import os
import subprocess
import time
from pathlib import Path

root = Path(__file__).resolve().parents[1]
if os.environ.get('XDG_RUNTIME_DIR') == f'/run/user/{os.getuid()}':
    raise SystemExit('Refusing daily desktop')
env = dict(os.environ, GTK_A11Y='atspi', GTK_IM_MODULE='none', GDK_BACKEND='wayland')
out = Path(os.environ['XDG_RUNTIME_DIR']) / 'context-check'
out.mkdir(exist_ok=True)
children = []
checks = []


def run(*argv):
    return subprocess.check_output(argv, env=env, text=True, timeout=4)


def key(s):
    keyboard.stdin.write(s + '\n')
    keyboard.stdin.flush()
    assert keyboard.stdout.readline().strip() == 'OK'
    time.sleep(.15)


def focus(pid):
    run('hyprctl', 'dispatch', 'focuswindow', f'pid:{pid}')
    time.sleep(.2)


def editor(text, password=False):
    p = subprocess.Popen([str(root/'build/test_editor'), str(out/f'{len(children)}.txt'), text]
                         + (['password'] if password else []), env=env,
                         stdout=(out/f'{len(children)}.log').open('w'), stderr=subprocess.STDOUT)
    children.append(p)
    time.sleep(.5)
    focus(p.pid)
    key('ctrl End')
    return p


def context(pid, limit=1024):
    return json.loads(run(str(root/'build/hyprvoice'), 'read-context', str(pid), str(limit)))


def check(name, condition):
    assert condition, name
    checks.append(name)


try:
    keyboard = subprocess.Popen([str(root/'build/test_keyboard')], env=env, stdin=subprocess.PIPE,
                                stdout=subprocess.PIPE, text=True)
    children.append(keyboard)
    assert keyboard.stdout.readline().strip() == 'READY'
    prefix = '我们正在开发 Hyprvoice，使用 Hyprland。'
    p = editor(prefix)
    value = context(p.pid)
    check('reads actual keyboard/editor text before caret', value['available'] and value['text'] == prefix)
    key('ctrl Home')
    check('excludes text after caret', context(p.pid)['text'] == '')
    key('ctrl End')
    key('type next')
    check('reflects later keyboard edits', context(p.pid)['text'] == prefix + 'next')
    check('Unicode context bounded by characters', context(p.pid, 5)['text'] == '。next')
    other = editor('另一个输入框，仅供测试。')
    check('isolates other window', context(other.pid)['text'] == '另一个输入框，仅供测试。')
    check('inactive editor cannot supply context', not context(p.pid)['available'])
    focus(p.pid)
    check('restores current edited buffer context', context(p.pid)['text'] == prefix + 'next')
    secret = editor('public-test-password', password=True)
    v = context(secret.pid)
    check('password text never read', v['protected'] and not v['available'] and v['text'] == '')
    print(json.dumps({'checks':checks,'passed':len(checks)}, ensure_ascii=False, indent=2))
    (out/'result.json').write_text(json.dumps({'checks':checks,'passed':len(checks)}, ensure_ascii=False, indent=2))
finally:
    for p in reversed(children):
        p.terminate()
        p.wait(timeout=5)
