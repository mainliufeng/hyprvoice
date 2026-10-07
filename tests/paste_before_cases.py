"""Public synthetic before-test for the old installed binary on the live fork."""
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



choose('contenteditable');boot(auto=True)
before=page_state();raw='公开测试：识别成功但粘贴命令失败。';set_transcripts(raw)
begin();old=finish('idle')
(evidence/'public-before-state.json').write_text(json.dumps({'state':old,'expected':raw,'before':before,'after':page_state()},ensure_ascii=False,indent=2))
check('before-recognition-success-but-no-real-input',
      (old['raw']==raw or old['text']==raw) and page_state()==before)
if old['error']:
    label('就绪');label('这次没有输入文字。请重试，或检查语音输入设置。')
    check('before-error-displayed-as-idle',old['error'].startswith('粘贴发送状态未知'))
else:
    check('before-invalid-dispatch-reply-treated-as-sent',old['text']=='已发送粘贴请求')
names={w['name'] for w in accessible()['widgets'] if w['showing']}
check('before-no-input-or-copy-recovery-controls',not ({'输入','复制'} & names))
photo('before-recognized-text-did-not-reach-editor')
