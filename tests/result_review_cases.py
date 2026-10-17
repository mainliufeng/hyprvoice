"""Production App/GTK with explicit capture+ASR doubles, private desktop only."""
import json
import time
from types import SimpleNamespace
from PIL import Image


def run(namespace):
    f = SimpleNamespace(**namespace)
    f.config.update(scene='raw', context={'enabled': False})

    def record(scenario, auto=True):
        f.focus(f.first); f.key('ctrl End')
        (f.runtime / 'recovery-scenario.json').write_text(json.dumps(scenario, ensure_ascii=False))
        f.boot(auto=auto)
        before = f.first_path.read_text()
        assert f.command('start')[0] == 0
        if not scenario.get('interrupt') or scenario.get('error_at_stop'):
            f.phase('recording')
            assert f.command('stop')[0] == 0
        result = f.phase('error' if not scenario.get('text') and not scenario.get('preview') else 'ready')
        time.sleep(.15)
        return result, before

    for name, scenario, expected in (
        ('device', {'interrupt': True, 'preview': '实时有效预览', 'text': '收尾完成的完整识别'}, '收尾完成的完整识别'),
        ('backlog', {'interrupt': True, 'capture_error': 'Audio processing cannot keep up', 'text': '队列有效音频识别'}, '队列有效音频识别'),
        ('finish-failure', {'interrupt': True, 'preview': '最后可用实时预览', 'finish_error': True}, '最后可用实时预览'),
        ('final-stop-error', {'interrupt': True, 'error_at_stop': True, 'text': '停止时发现设备错误'}, '停止时发现设备错误'),
        ('no-preview-backend', {'interrupt': True, 'text': '仅收尾后端的有效结果'}, '仅收尾后端的有效结果'),
        ('no-result', {'interrupt': True, 'finish_error': True}, ''),
        ('normal-stop-finish-failure', {'preview': '正常停止后的可用预览', 'finish_error': True}, '正常停止后的可用预览'),
    ):
        s, before = record(scenario)
        trace = json.loads((f.runtime / 'recovery-trace.json').read_text())
        f.check(name+'-drains-once-finalizes-once', trace['samples'] == 960 and trace['finish'] == 1)
        f.check(name+'-retains-most-available-body', s['effective_text'] == expected and s['incomplete'] and s['manual_confirmation'])
        f.check(name+'-never-auto-input-or-model', f.first_path.read_text() == before and len(f.server.bodies) == 0)
        f.check(name+'-no-fake-alternatives', not s['asr_parts'])
        if expected:
            f.photo('recovery-'+name)
            f.command('copy'); f.phase('idle')
            f.check(name+'-copy-body-without-warning', f.run('wl-paste', '--no-newline') == expected)
            f.check(name+'-copy-clears-consumed-warning', not f.state()['incomplete'] and not f.state()['recognition_warning'])

    # Two actual recognizer outputs are kept separately. Text processing is not
    # offered through these ASR selectors.
    stream, refined = '项目预算2400元，不批准。', '项目预算2400元，暂不批准。'
    s, before = record({'text': refined, 'streaming': stream, 'refined': refined}, auto=False)
    f.check('actual-two-source-candidates', s['asr_candidates_active'] and s['asr_streaming'] == stream and s['asr_refined'] == refined)
    f.photo('asr-default')
    index = next(i for i, p in enumerate(s['asr_parts']) if p['selectable'])
    def choice(state, source):
        return json.dumps({'session_id': state['session_id'], 'base_revision': state['result_revision'], 'index': index, 'source': source})
    f.command('select-candidate', choice(s, 'streaming'))
    selected = f.state()
    f.check('candidate-switch-preview-only', selected['effective_text'] == stream and f.first_path.read_text() == before and selected['raw'] == refined)
    f.check('stale-candidate-rejected', f.command('select-candidate', choice(s, 'refined'))[0] == 1 and f.state()['effective_text'] == stream)
    f.command('select-candidate', choice(selected, 'refined'))
    f.check('candidate-switch-back', f.state()['effective_text'] == refined)
    f.photo('candidate-reading')
    # GTK4's AT-SPI character extents return zero width here. Locate the actual
    # rendered amber fragment in the full-resolution private output screenshot.
    with Image.open(f.evidence/'candidate-reading.png') as shot:
        rgb = shot.convert('RGB')
        pixels = [(x, y) for y in range(rgb.height) for x in range(rgb.width)
                  if rgb.getpixel((x, y)) == (255, 244, 214)]
    assert pixels, 'The default ASR fragment must be visibly marked'
    x = (min(p[0] for p in pixels) + max(p[0] for p in pixels)) // 2
    y = (min(p[1] for p in pixels) + max(p[1] for p in pixels)) // 2
    (f.evidence/'candidate-click-geometry.json').write_text(json.dumps({'rendered_fragment_center': [x, y]}))
    f.key(f'click {x} {y}')
    f.photo('candidate-after-pointer')
    f.label('音频识别来源 · 仅此片段不同')
    f.check('default-asr-fragment-is-directly-clickable', True)
    f.photo('candidate-expanded')
    # Activate the actual lower candidate button, not the IPC selection API.
    alternative = next(p['streaming'] for p in f.state()['asr_parts'] if p['selectable'])
    f.click((alternative or '〔不含此段文字〕') + '\n实时识别 · 点击采用')
    f.wait(lambda: f.state()['effective_text'] == stream)
    f.check('lower-candidate-button-selects-without-input', f.state()['effective_text'] == stream and f.first_path.read_text() == before)
    f.command('select-candidate', choice(f.state(), 'refined'))

    # No-op edit pause must not create a future stale draft after a body action.
    assert f.command('edit-begin')[0] == 0
    assert f.command('edit-cancel')[0] == 0
    f.check('unchanged-edit-clears-draft', not f.state()['draft_pending'] and f.state()['draft_session_id'] == 0)
    s = f.state(); f.command('select-candidate', choice(s, 'streaming'))
    f.check('body-change-after-noop-edit-not-blocked', not f.state()['draft_pending'])
    f.focus(f.first)
    assert f.command('edit-begin')[0] == 0
    manual = '手动编辑：不要付款，预算1.5万元。\n第二行保留。'
    assert f.command('edit-draft', json.dumps({'text': manual})) [0] == 0
    f.command('edit-cancel')
    paused = f.state()
    for action, arg in [('copy', ''), ('revise', ''), ('insert-current', ''), ('select-candidate', choice(paused, 'refined'))]:
        f.check('pending-draft-blocks-'+action, f.command(action, arg)[0] == 1 and f.state()['draft_text'] == manual and f.state()['effective_text'] == stream)
    f.photo('draft-retained')
    f.focus(f.first); assert f.command('edit-begin')[0] == 0
    f.check('resume-draft-not-overwritten', f.state()['draft_text'] == manual)
    assert f.command('edit-save')[0] == 0
    saved = f.state()
    f.check('manual-save-keeps-sources-and-disables-old-slots', saved['effective_text'] == manual and saved['raw'] == refined and not saved['asr_candidates_active'] and not saved['draft_pending'] and saved['manual_confirmation'])
    f.photo('manual-saved')
    f.command('copy'); f.phase('idle')
    f.check('manual-copy-only-body', f.run('wl-paste', '--no-newline') == manual)

    # Explicit user cancellation discards current capture, not a recovered result.
    f.focus(f.first)
    (f.runtime/'recovery-scenario.json').write_text(json.dumps({'preview': '主动取消不保留'}))
    f.boot(auto=False); f.command('start'); f.phase('recording'); f.command('cancel')
    cancelled = f.phase('idle')
    f.check('user-cancel-separate-from-device-recovery', not cancelled['effective_text'] and not cancelled['incomplete'])

    # A duration limit is also an interruption, never a complete auto-input.
    f.focus(f.first)
    f.config['max_recording_seconds'] = 1
    (f.runtime/'recovery-scenario.json').write_text(json.dumps({'text': '时长上限前正文'}))
    f.boot(auto=True); before = f.first_path.read_text(); f.command('start')
    limited = f.phase('ready')
    f.check('duration-limit-requires-confirmation', limited['incomplete'] and limited['manual_confirmation'] and f.first_path.read_text() == before)
    # Never send a partial F9 instruction on capture failure, keep the base body.
    f.focus(f.first)
    (f.runtime/'recovery-scenario.json').write_text(json.dumps({'interrupt': True, 'preview': '不完整修改要求', 'text': '不能发送此指令'}))
    requests = len(f.server.bodies)
    assert f.command('revise')[0] == 0
    failed_revision = f.phase('ready')
    f.check('interrupted-f9-preserves-body-no-model', failed_revision['effective_text'] == limited['effective_text'] and len(f.server.bodies) == requests and failed_revision['revision_error'])
    f.command('discard'); f.phase('idle')
    f.config.pop('max_recording_seconds')

    # Scene processing keeps provenance but never masquerades as ASR candidates.
    f.config['scene'] = 'correct'
    for retry_output in (refined, '重试得到的文字处理新版。'):
        f.plan('failure')
        failed, before = record({'text': refined, 'streaming': stream, 'refined': refined}, auto=False)
        f.check('failed-processing-retains-real-asr-slots-'+str(retry_output == refined), failed['asr_candidates_active'] and failed['retry_available'])
        f.plan('success', output=retry_output)
        f.command('retry'); retried = f.phase('ready')
        if retry_output == refined:
            f.check('same-text-retry-does-not-fake-text-change', retried['asr_candidates_active'] and retried['result_source'] == 'asr' and not retried['change_before'])
        else:
            f.check('changed-retry-invalidates-asr-slots', not retried['asr_candidates_active'] and retried['result_source'] == 'processed' and retried['change_before'] == refined)
            stale = choice(retried, 'streaming')
            f.check('retry-old-candidate-cannot-overwrite-body', f.command('select-candidate', stale)[0] == 1 and f.state()['effective_text'] == retry_output)
            f.label(retry_output); f.photo('retry-processed-reading')
            f.command('copy'); f.phase('idle')
            f.check('retry-copy-matches-displayed-body', f.run('wl-paste', '--no-newline') == retry_output)
        f.command('discard'); f.phase('idle')
    f.plan('success', output='模型文字润色后的结果。')
    s, before = record({'text': refined, 'streaming': stream, 'refined': refined}, auto=False)
    f.check('processed-diff-is-not-audio-ambiguity', s['result_source'] == 'processed' and not s['asr_candidates_active'] and s['change_before'] == refined and s['asr_streaming'] == stream)
    f.photo('processed-version-diff')
    f.click('查看修改'); f.photo('processed-version-comparison')
    f.focus(f.first)
    (f.runtime/'recovery-scenario.json').write_text(json.dumps({'text': '把模型改成手动，删除文字，末尾增加下一步。'}))
    f.plan('success', output='手动润色后的结果。下一步。')
    assert f.command('revise')[0] == 0
    f.phase('recording'); f.command('stop'); revised = f.phase('ready')
    f.check('f9-new-version-starts-in-reading-mode', revised['result_source'] == 'voice')
    f.photo('voice-revision-reading')
    f.click('查看修改'); f.photo('voice-revision-comparison')
    f.command('discard'); f.phase('idle')
    f.config['scene'] = 'raw'
    long_body = '长文阅读与完整编辑验证：保留金额1.5万元，不批准付款。\n' * 80
    long_state, before = record({'text': long_body}, auto=False)
    f.photo('long-reading')
    f.check('long-body-not-truncated', long_state['effective_text'] == long_body)
    f.label('确认输入')
    f.focus(f.first); assert f.command('edit-begin')[0] == 0
    f.photo('long-editing')
    f.check('long-editor-full-draft', f.state()['draft_text'] == long_body)
    f.command('edit-cancel')
    f.check('no-unplanned-http', f.server.unexpected == 0)
