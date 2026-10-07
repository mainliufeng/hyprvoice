"""Real Chromium input acceptance on a private public HTML chat fixture.

Executed by browser_desktop_test.py with its production App/zero-audio/HTTP
fixture. Page events report exact public text and input/submit counts via title;
no DevTools, browser profile reuse, real microphone or external model is used.
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

first = target_guard()
check('default-chrome-first-query-lazy-web-target-reliable',
      first['reliable'] and first['toolkit']=='chromium')
check('browser-guard-contains-only-metadata-and-digests',
      'text' not in first and first['characters']==0 and first['caret']==0)
result['chrome_force_accessibility_flag'] = False
result['public_history_nodes'] = 1200
result['first_target'] = first
boot(auto=True)

# Ordinary textarea and a nested-paragraph rich chat editor both insert the
# complete displayed Unicode/multiline result once, without sending the form.
for name in ('textarea', 'contenteditable'):
    choose(name)
    guard = target_guard()
    check(name+'-focused-web-control-reliable', guard['reliable'])
    raw = '你好，会议下午三点开始。🙂\nPlease keep 2400 dollars unchanged.'
    set_transcripts(raw)
    index=plan('success', output=raw)
    begin(); finish('idle')
    inserted = same_page(raw)
    check(name+'-automatic-input-exactly-once',
          inserted['inputs']==1 and inserted['submits']==0 and inserted['paste']==raw and
          len(server.bodies)==index+1)
    check(name+'-result-consumed-no-duplicate-paste',
          command('commit')[0]==1 and command('insert-current')[0]==1 and
          page_state()==inserted)

# The context query stays within the focused editor and configured character
# limit even with a large transcript history elsewhere in the document.
choose('contenteditable'); key('key F10')
rich_guard = target_guard()
history = json.loads(run(str(candidate), 'read-context', str(browser.pid), '18'))
(evidence/'public-rich-context.json').write_text(json.dumps(
    {'guard':rich_guard,'context':history,'page':page_state()},ensure_ascii=False,indent=2))
subprocess.run(['/usr/bin/python3',str(repo/'tests/browser_rich_probe.py'),str(browser.pid),str(evidence/'public-rich-tree.json')],env=env,check=True,timeout=10)
check('nested-paragraph-unicode-target-reliable', rich_guard['reliable'])
check('browser-context-only-bounded-focused-editor',
      history['available'] and not history['protected'] and
      len(history['text'])<=18 and 'second line.' in history['text'] and
      'chat history' not in history['text'] and 'public-test-password' not in history['text'])
key('key Left'); inside=target_guard()
check('paragraph-internal-caret-maps-to-real-text-offset',
      inside['reliable'] and inside['characters']==rich_guard['characters'] and
      inside['caret']==rich_guard['caret']-1 and inside['editor_digest']!=rich_guard['editor_digest'])
raw='段落内部光标往返的公开测试。';set_transcripts(raw);index=len(server.bodies)
begin();key('key Left');key('key Right');after=target_guard();moved=finish()
check('paragraph-caret-roundtrip-restores-exact-snapshot',
      inside['editor_digest']==after['editor_digest'] and inside['caret']==after['caret'])
check('paragraph-descendant-events-sticky-no-cloud-no-auto-input',
      len(server.bodies)==index and moved['raw']==raw and command('commit')[0]==1)
cancel_result()
key('ctrl Home');key('shift Right');selected_guard=target_guard()
check('paragraph-internal-selection-visible-to-guard',
      selected_guard['reliable'] and selected_guard['selections']==[[0,1]])
raw='普通听写不能覆盖段落选区。';set_transcripts(raw);index=plan('success',output=raw)
before_selected=page_state();begin();ready=finish()
check('normal-dictation-never-overwrites-paragraph-selection',
      len(server.bodies)==index+1 and page_state()==before_selected and
      command('commit')[0]==1 and command('insert-current')[0]==1)
cancel_result()

# A processing error keeps the local transcript. The actual production GTK
# button inputs it into the browser; there is no clipboard step for the user.
focus(browser); key('key F1'); choose('contenteditable')
boot(auto=False)
raw='这是公开测试文字，不要付款。🙂'
set_transcripts(raw); index=plan('failure')
begin(); failed=finish()
check('browser-processing-failure-retains-local-text',
      failed['raw']==raw and failed['text']==raw and failed['insertion_status']=='available')
label('输入'); photo('browser-input-ready')
click('输入'); phase('idle')
inserted=same_page(raw)
check('one-click-ui-input-to-chat-editor-exactly-once',
      inserted['inputs']==1 and inserted['submits']==0 and inserted['paste']==raw and len(server.bodies)==index+1)

# Move the caret during recording. Automatic input and a request containing
# the earlier context remain blocked; a user click can input at the new caret.
choose('contenteditable')
before=page_state()
raw='补充一句。'
set_transcripts(raw); index=len(server.bodies)
begin(); key('key F7'); moved=finish()
check('browser-caret-move-no-cloud-no-automatic-input',
      len(server.bodies)==index and moved['raw']==raw and page_state()['text']==before['text'])
check('old-browser-position-cannot-commit', command('commit')[0]==1)
click('输入'); phase('idle')
inserted=same_page(raw+before['text'])
check('manual-browser-input-uses-current-caret-once',
      inserted['inputs']==before['inputs']+1 and inserted['submits']==0)

# Changing focused editor is observable even in the same browser window.
choose('contenteditable')
before=page_state(); raw='换到第二个输入框。'
set_transcripts(raw); index=len(server.bodies)
begin(); choose('other'); changed=finish()
check('same-window-other-browser-control-blocks-auto-and-cloud',
      len(server.bodies)==index and page_state()['text']=='' and changed['raw']==raw)
check('same-window-other-control-rejects-original-commit', command('commit')[0]==1)
click('输入'); phase('idle')
inserted=same_page(raw)
check('explicit-click-retargets-current-browser-editor-once',
      inserted['inputs']==1 and inserted['submits']==0)
choose('contenteditable')
check('original-browser-editor-remains-unchanged', page_state()['text']==before['text'])

# Equal-length buffer changes must be caught by digest, not just caret/count.
key('key F10'); before=target_guard()
raw='不能悄悄覆盖已编辑文字。'
set_transcripts(raw); index=len(server.bodies)
begin(); key('key F11'); changed=finish()
after=target_guard()
check('equal-length-browser-edit-changes-digest',
      before['characters']==after['characters'] and before['caret']==after['caret'] and
      before['digest']!=after['digest'])
check('equal-length-browser-edit-no-cloud-no-auto-input',
      len(server.bodies)==index and command('commit')[0]==1 and
      page_state()['text'].startswith('PUBLIC FIRST'))
cancel_result()

# A password is recognised before requesting context or copying selection.
choose('password'); password=target_guard()
context=json.loads(run(str(candidate),'read-context',str(browser.pid),'1024'))
check('browser-password-protected-without-value',
      password['protected'] and not password['reliable'] and
      'text' not in password and context['protected'] and context['text']=='')
check('browser-password-command-rejected-before-selection-copy',command('command')[0]==1)
raw='公开合成语音，不能输入密码框。'
set_transcripts(raw); index=len(server.bodies)
begin(); protected=finish()
check('browser-password-no-cloud-no-automatic-input',
      len(server.bodies)==index and page_state()['inputs']==0 and
      protected['context']=='' and protected['raw']==raw)
check('browser-password-current-position-input-rejected',
      command('insert-current')[0]==1 and command('commit')[0]==1)
cancel_result()

# A password focus roundtrip retains the privacy boundary even when start and
# end focus snapshots are the same ordinary editor.
choose('contenteditable'); before=target_guard()
raw='录音中途切换密码框的公开测试。'
set_transcripts(raw); index=len(server.bodies)
begin(); choose('password'); choose('contenteditable'); protected=finish()
after=target_guard()
check('browser-password-roundtrip-snapshots-same',
      before['control']==after['control'] and before['digest']==after['digest'] and
      before['caret']==after['caret'])
check('browser-password-roundtrip-sticky-no-cloud-no-auto-input',
      len(server.bodies)==index and protected['raw']==raw and command('commit')[0]==1)
cancel_result()

# A window switch cannot send text into the new window or reuse old context.
choose('contenteditable'); raw='另一个窗口不能收到这段公开文字。'
set_transcripts(raw); index=len(server.bodies)
begin(); other_window, other_path=editor('Public separate window.', 'separate-window')
changed=finish()
check('browser-to-other-window-no-cloud-no-auto-input',
      len(server.bodies)==index and other_path.read_text()=='Public separate window.' and
      changed['raw']==raw and command('insert-current')[0]==1)
cancel_result(); focus(browser)

# Existing selection modification also works on a normal browser textarea,
# while an empty or moved selection cannot be silently replaced.
choose('textarea'); key('key F12'); key('ctrl a')
selected=page_state()['text']; instruction='将公开选中文字整理为一句话。'
copy_guard=target_guard();key('ctrl c')
public_copy=run('/usr/bin/wl-paste','--no-newline','--type','text')
check('emoji-selection-range-matches-exact-browser-clipboard',
      copy_guard['reliable'] and copy_guard['selections']==[[0,len(selected)]] and public_copy==selected)
(evidence/'public-selection-copy.json').write_text(json.dumps(
    {'guard':copy_guard,'expected':selected,'clipboard':public_copy},ensure_ascii=False,indent=2))
subprocess.run(['/usr/bin/python3',str(repo/'tests/browser_rich_probe.py'),str(browser.pid),str(evidence/'public-selection-tree.json')],env=env,check=True,timeout=10)

set_transcripts(instruction); output='公开选区修改结果🙂。'
index=plan('success',output=output)
begin('command'); ready=finish()
check('browser-command-uses-selected-text-not-spoken-instruction',
      ready['command_mode'] and ready['text']==output and ready['raw']==instruction and
      len(server.bodies)==index+1)
click('替换选中文字'); phase('idle')
inserted=same_page(output)
check('browser-command-replaces-selection-once-without-submit',
      inserted['inputs']==1 and inserted['submits']==0 and command('commit')[0]==1)
key('ctrl End')
check('browser-empty-selection-command-rejected',command('command')[0]==1)

# Paragraph selection must agree with the browser clipboard before model use.
choose('contenteditable');key('key F10');key('ctrl a')
rich_selection=target_guard();before=page_state();output='段落选区修改结果🙂。'
(evidence/'public-rich-selection-guard.json').write_text(json.dumps(rich_selection,indent=2))
subprocess.run(['/usr/bin/python3',str(repo/'tests/browser_rich_probe.py'),str(browser.pid),str(evidence/'public-rich-selection-tree.json')],env=env,check=True,timeout=10)
set_transcripts('整理公开段落。');index=plan('success',output=output)
begin('command');ready=finish()
check('rich-paragraph-selection-copy-verified-before-model',
      rich_selection['reliable'] and len(rich_selection['selections'])==1 and
      ready['command_mode'] and ready['text']==output and len(server.bodies)==index+1)
click('替换选中文字');phase('idle');inserted=same_page(output)
check('rich-paragraph-selection-replaced-exactly-once',
      inserted['inputs']==before['inputs']+1 and inserted['submits']==0 and inserted['paste']==output)

# Changes in the conversation history are not changes in the focused input.
boot(auto=True);choose('contenteditable');before=page_state()
raw='只输入这一句公开测试。';set_transcripts(raw);index=plan('success',output=raw)
begin();key('key F8');finish('idle');inserted=same_page(before['text']+raw)
check('background-chat-history-change-does-not-block-focused-input',
      inserted['inputs']==before['inputs']+1 and inserted['submits']==0 and len(server.bodies)==index+1)

# Non-editable controls and browser chrome are never treated as a web editor.
choose('readonly'); guard=target_guard()
check('browser-readonly-not-reliable',not guard['reliable'])
raw='只读控件的公开测试。';set_transcripts(raw);index=len(server.bodies)
begin(); complete=finish()
check('browser-readonly-no-cloud-no-input',
      len(server.bodies)==index and page_state()['inputs']==0 and command('insert-current')[0]==1)
cancel_result()
choose('contenteditable'); key('ctrl l'); guard=target_guard()
check('browser-address-bar-not-accepted-as-chat-editor',not guard['reliable'])
raw='地址栏不能收到这段公开文字。';set_transcripts(raw);index=len(server.bodies)
begin(); complete=finish()
check('browser-address-bar-no-cloud-no-input',
      len(server.bodies)==index and command('insert-current')[0]==1)
cancel_result()
