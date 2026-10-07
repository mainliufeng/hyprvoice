"""Result controls acceptance using the existing private production App fixture.

All editors, transcripts, clipboard contents and HTTP responses are synthetic.
"""
from types import SimpleNamespace


def run(namespace, mode):
    f = SimpleNamespace(**namespace)
    if mode == 'before':
        f.boot(auto=False)
        f.set_transcripts(f.raw)
        f.plan('success', output=f.processed)
        f.begin(); f.finish()
        before = f.first_path.read_text()
        f.label('使用原文')
        f.photo('01-original-choices')
        f.click('使用原文')
        f.phase('idle')
        f.wait(lambda: f.first_path.read_text() == before + f.raw)
        f.check('old-use-original-directly-inserts', f.first_path.read_text() == before + f.raw)

        target, path = f.editor('Public unsupported input.', 'unsupported', readonly=True)
        f.boot(auto=False)
        f.set_transcripts(f.raw)
        count = len(f.server.bodies)
        f.begin(); f.finish()
        f.click('插入文字')
        f.wait(lambda: bool(f.state()['error']))
        f.label('使用原文')
        f.photo('02-original-insert-blocked')
        f.check('old-insert-button-does-not-insert', path.read_text() == 'Public unsupported input.')
        f.check('old-duplicate-choice-visible', f.state()['raw'] == f.state()['text'])
        f.check('old-unknown-target-no-text-http', len(f.server.bodies) == count)
        f.click('复制结果')
        f.phase('idle')
        f.check('old-copy-only-consumes-result', f.run('/usr/bin/wl-paste', '--no-newline') == f.raw)
        return
    assert mode == 'after'

    def visible_names():
        return {w['name'] for w in f.accessible()['widgets'] if w['showing']}

    # A positively identified read-only input retains a copy action.
    target, path = f.editor('Public unsupported input.', 'unsupported', readonly=True)
    f.boot(auto=True)
    f.set_transcripts(f.raw)
    count = len(f.server.bodies)
    f.begin(); complete = f.finish()
    f.label('复制')
    names = visible_names()
    f.check('readonly-input-copy-primary-no-insert-control',
            complete['insertion_status'] == 'unknown' and
            not ({'使用原文', '插入文字', '输入', '确认输入位置',
                  '撤销修改'} & names))
    f.check('readonly-input-explains-manual-paste',
            '这里暂时不能直接输入。请点击“复制”，再到输入框粘贴。' in names)
    f.check('readonly-input-keeps-no-http-no-auto-input',
            len(f.server.bodies) == count and path.read_text() == 'Public unsupported input.')
    f.photo('01-copy-instead-of-blocked-insert')
    f.click('复制'); f.phase('idle')
    f.label('已复制')
    f.label('文字已复制到剪贴板')
    f.photo('02-visible-copied-feedback')
    f.check('copy-feedback-and-exact-private-clipboard',
            f.run('/usr/bin/wl-paste', '--no-newline') == f.raw and
            path.read_text() == 'Public unsupported input.')
    f.check('copied-result-cannot-be-reused', f.command('select-text', 'raw')[0] == 1 and
            f.command('copy')[0] == 1 and f.command('insert-current')[0] == 1)
    f.command('cancel'); f.phase('idle')

    # Choosing a text version is only a preview, including switching back.
    f.focus(f.first); f.key('ctrl End')
    f.boot(auto=False)
    f.set_transcripts(f.raw); f.plan('success', output=f.processed)
    f.begin(); f.finish()
    before = f.first_path.read_text()
    requests = len(f.server.bodies)
    f.click('撤销修改')
    f.wait(lambda: f.state()['preferred_raw'])
    f.label(f.raw); f.photo('03-original-text-preview-only')
    f.check('preview-raw-never-inserts-or-resends', f.first_path.read_text() == before and
            f.state()['phase'] == 'ready' and len(f.server.bodies) == requests)
    f.click('恢复修改')
    f.wait(lambda: not f.state()['preferred_raw'])
    f.label(f.processed)
    f.check('processed-preview-never-inserts', f.first_path.read_text() == before)
    f.click('撤销修改'); f.wait(lambda: f.state()['preferred_raw'])
    f.click('复制'); f.phase('idle')
    f.check('copy-uses-exact-displayed-raw-text',
            f.run('/usr/bin/wl-paste', '--no-newline') == f.raw and f.first_path.read_text() == before)
    f.command('cancel'); f.phase('idle')

    # The separate explicit insert must use the displayed version, exactly once.
    f.boot(auto=False)
    f.set_transcripts(f.raw); f.plan('success', output=f.processed)
    f.begin(); f.finish()
    before = f.first_path.read_text()
    f.click('撤销修改'); f.wait(lambda: f.state()['preferred_raw'])
    f.click('输入'); f.phase('idle')
    f.wait(lambda: f.first_path.read_text() == before + f.raw)
    f.check('displayed-raw-inserted-exactly-once',
            f.first_path.read_text() == before + f.raw and f.command('insert-current')[0] == 1)
    f.command('cancel'); f.phase('idle')

    # When processing failed, the already displayed raw text needs no duplicate choice.
    f.boot(auto=False)
    f.set_transcripts(f.raw); f.plan('failure')
    f.begin(); f.finish()
    f.label('输入')
    f.check('identical-raw-and-result-no-ambiguous-selector',
            not ({'撤销修改', '恢复修改', '使用原文'} & visible_names()))
    before = f.first_path.read_text()
    f.click('输入'); f.phase('idle')
    f.wait(lambda: f.first_path.read_text() == before + f.raw)
    f.check('fallback-current-text-has-working-insert', f.first_path.read_text() == before + f.raw)
    f.command('cancel'); f.phase('idle')

    # One explicit input click checks and binds the current caret internally.
    # The user never needs to understand or operate position-review controls.
    f.boot(auto=False)
    f.set_transcripts(f.raw)
    count = len(f.server.bodies)
    f.begin(); f.key('type changed'); f.finish()
    changed = f.first_path.read_text()
    f.label('输入'); f.photo('04-one-clear-input-action')
    f.check('changed-capture-does-not-send-or-insert', len(f.server.bodies) == count and
            f.state()['text'] == f.raw and f.first_path.read_text() == changed)
    f.check('no-internal-position-buttons', not ({'确认输入位置', '核对当前位置',
            '插入到此处', '查看识别文字', '查看处理后文字'} & visible_names()))
    f.key('key Left')
    f.click('输入'); f.phase('idle')
    f.wait(lambda: f.first_path.read_text() == changed[:-1] + f.raw + changed[-1:])
    f.check('one-click-input-at-current-caret-exactly-once',
            f.command('insert-current')[0] == 1)
    f.command('cancel'); f.phase('idle'); f.key('ctrl End')

    # Switching text invalidates a position token, so its reviewed text cannot
    # silently differ from the preview. Test real GTK buttons through the flow.
    f.boot(auto=False)
    f.set_transcripts(f.raw); f.plan('success', output=f.processed)
    f.begin(); f.finish()
    f.key('type changed')
    before = f.first_path.read_text()
    assert f.command('review')[0] == 0
    f.wait(lambda: bool(f.state()['review_token']))
    token = f.state()['review_token']
    f.click('撤销修改'); f.wait(lambda: f.state()['preferred_raw'])
    f.check('preview-switch-invalidates-position-token', not f.state()['review_token'] and
            f.first_path.read_text() == before and f.command('confirm', token)[0] == 1)
    f.click('输入'); f.phase('idle')
    f.wait(lambda: f.first_path.read_text() == before + f.raw)
    f.check('reconfirmed-preview-delivers-displayed-text-only', f.first_path.read_text() == before + f.raw)
    f.command('cancel'); f.phase('idle')

    # The UI fix does not create an insertion path into protected fields.
    protected, protected_path = f.editor('public-synthetic-password', 'protected', password=True)
    f.boot(auto=True)
    f.set_transcripts(f.raw)
    count = len(f.server.bodies)
    f.begin(); complete = f.finish()
    f.label('复制')
    f.check('password-keeps-copy-only-and-no-http',
            complete['insertion_status'] == 'protected' and len(f.server.bodies) == count and
            complete['phase'] == 'ready' and not protected_path.exists())
    f.check('password-insert-still-rejected', f.command('insert-current')[0] == 1 and
            f.command('review')[0] == 1)
    f.command('cancel'); f.phase('idle')

    # Failed clipboard ownership must keep the text and show a retryable error,
    # never the successful-copy notice. The second explicit click can succeed.
    f.focus(target)
    original_path = f.env['PATH']
    bin_dir = f.runtime/'copy-failure-bin'
    bin_dir.mkdir()
    counter = f.runtime/'copy-attempt-count'
    counter.write_text('0')
    wrapper = bin_dir/'wl-copy'
    wrapper.write_text("#!/usr/bin/python3\nimport os,pathlib,sys\np=pathlib.Path(os.environ['XDG_RUNTIME_DIR'])/'copy-attempt-count'\nn=int(p.read_text())+1\np.write_text(str(n))\nif n==1:sys.exit(1)\nos.execv('/usr/bin/wl-copy',['/usr/bin/wl-copy',*sys.argv[1:]])\n")
    wrapper.chmod(0o700)
    f.env['PATH'] = str(bin_dir)+':'+original_path
    f.boot(auto=False)
    f.run('/usr/bin/wl-copy', 'Public clipboard before failed copy')
    f.set_transcripts(f.raw); f.begin(); f.finish()
    f.click('复制')
    f.wait(lambda: f.state()['error'].startswith('复制失败'))
    f.check('copy-failure-retains-result-without-success-notice',
            f.state()['phase'] == 'ready' and f.state()['text'] == f.raw and
            '已复制' not in visible_names() and
            f.run('/usr/bin/wl-paste', '--no-newline') == 'Public clipboard before failed copy')
    f.wait(lambda: any('没能复制，文字还在这里。请再点一次“复制”。' in name
                       for name in visible_names()))
    f.check('copy-failure-explains-retry', True)
    f.photo('06-copy-failure-retains-text')
    f.click('复制'); f.phase('idle'); f.label('已复制')
    f.check('explicit-retry-copy-succeeds-once', counter.read_text() == '2' and
            f.run('/usr/bin/wl-paste', '--no-newline') == f.raw)
    f.env['PATH'] = original_path
    f.command('cancel'); f.phase('idle')
    f.check('no-unplanned-http-requests', f.server.unexpected == 0)
