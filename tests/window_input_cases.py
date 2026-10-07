"""Ordinary dictation must work with no accessibility registration in the target.

Uses private Chrome/Electron, fixed Unicode text, local mock ASR and zero audio.
The production App and its real GTK input button remain on the QA accessibility
bus; the target app is launched on a separate private bus with no registry.
"""
import re
import urllib.parse

def page_state():
    title = browser_client()['title'].removesuffix(' - Google Chrome')
    match = re.fullmatch(r'HV-QA ([a-z]+) (\d+) (\d+)(?: (.*))?', title)
    assert match, title
    payload=json.loads(urllib.parse.unquote(match[4]))
    return {'field': match[1], 'inputs': int(match[2]), 'submits': int(match[3]),
            'text':payload['text'], 'paste':payload['paste']}

def choose(name):
    focus(browser)
    key('key ' + {'textarea':'F2', 'password':'F3', 'contenteditable':'F4',
                 'other':'F5', 'readonly':'F6'}[name])
    wait(lambda: page_state()['field'] == name)

def target_guard():
    return json.loads(run(str(candidate), 'read-target', str(browser.pid)))

def same_page(expected):
    try:
        return wait(lambda: (p if (p := page_state())['text'] == expected else None))
    except RuntimeError:
        (evidence/'public-page-mismatch.json').write_text(json.dumps(
            {'expected':expected, 'actual':page_state()},ensure_ascii=False,indent=2))
        photo('public-page-mismatch')
        raise

def cancel_result():
    assert command('cancel')[0] == 0
    phase('idle')


first=target_guard()
check('target-not-registered-on-input-accessibility-bus',not first['reliable'] and first['reason']=='zero-focus')
result['target_editor_metadata_unavailable']=True
result['first_target']=first
for name in ('textarea','contenteditable'):
    choose(name);boot(auto=True)
    before=page_state();raw='公开语音输入🙂。\nKeep 2400 unchanged.'
    set_transcripts(raw);count=len(server.bodies)
    begin();complete=finish('idle');inserted=same_page(before['text']+raw)
    check(name+'-no-accessibility-auto-input-exactly-once',inserted['inputs']==before['inputs']+1 and inserted['submits']==0 and inserted['paste']==raw)
    check(name+'-no-context-or-model-request',len(server.bodies)==count and complete['context']=='')
    check(name+'-result-consumed-no-repeat',command('commit')[0]==1 and command('insert-current')[0]==1 and page_state()==inserted)
    boot(auto=False);manual=' 手动确认的公开文字🙂。'
    set_transcripts(manual);begin();ready=finish()
    check(name+'-missing-metadata-keeps-input-action',ready['insertion_status']=='window' and ready['text']==manual)
    label('输入');click('输入');phase('idle');inserted=same_page(before['text']+raw+manual)
    check(name+'-one-click-input-without-copy',inserted['inputs']==before['inputs']+2 and inserted['submits']==0 and command('insert-current')[0]==1 and len(server.bodies)==count)
    check(name+'-selection-command-still-rejected-before-copy',command('command')[0]==1)

# Sticky window changes block automatic insertion, even if the original app
# returns before recording finishes. A deliberate input click can then recover.
choose('contenteditable');boot(auto=True)
before=page_state();raw='窗口往返后的公开文字。';set_transcripts(raw);count=len(server.bodies)
begin();other_window,other_path=editor('Public separate window.', 'window-only-other')
focus(browser);ready=finish()
check('window-roundtrip-no-auto-input-in-either-window',page_state()==before and other_path.read_text()=='Public separate window.' and ready['raw']==raw and len(server.bodies)==count)
label('输入');click('输入');phase('idle');inserted=same_page(before['text']+raw)
check('explicit-return-to-original-window-input-once',inserted['inputs']==before['inputs']+1 and inserted['submits']==0 and command('insert-current')[0]==1)

# Remaining in a different window rejects even the explicit input action.
boot(auto=True);before=page_state();set_transcripts(raw);begin();focus(other_window);ready=finish()
check('different-window-explicit-input-rejected',command('insert-current')[0]==1 and other_path.read_text()=='Public separate window.' and ready['raw']==raw)
focus(browser);check('different-window-did-not-alter-original',page_state()==before)
cancel_result()
check('window-only-dictation-never-sends-editor-context-or-text-model-http',len(server.bodies)==count and server.unexpected==0)
