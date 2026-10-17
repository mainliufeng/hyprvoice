"""Real App focus acceptance; ASR/HTTP are synthetic, no direct result editing."""
import json
import time
from types import SimpleNamespace
from pointer import Pointer


def run(namespace):
    f = SimpleNamespace(**namespace)
    f.boot(auto=False)
    pointer = Pointer(namespace, "HV-QA")
    source = 'synthetic focus test original.'
    edited = 'synthetic body saved through the real overlay editor.'
    revised = 'synthetic body revised after saving the real overlay editor.'
    f.set_transcripts(source)
    f.plan('success', output=source)
    f.begin(); f.finish()
    before = f.first_path.read_text()
    f.photo('focus-01-ready-before-edit')

    # Accessible names are the agreed prototype contract. A missing production
    # control is a failure/blocker, never bypassed by edit-result IPC.
    pointer.click('编辑正文')
    f.wait(lambda: any(w['name'] == '编辑完整正文' and w['showing']
                      for w in f.accessible()['widgets']))
    pointer.click('编辑完整正文')
    f.key('ctrl a'); f.key('type ' + edited)
    try:
        f.wait(lambda: f.state().get('draft_text') == edited, seconds=4)
    except Exception:
        f.photo('primary-editor-typing-failure')
        (f.evidence/'primary-editor-typing-failure.json').write_text(json.dumps(
            {'state':f.state(),'widgets':f.accessible(),'primary_body':f.first_path.read_text(),
             'expected':edited},ensure_ascii=False,indent=2))
        raise
    f.check('real-overlay-typing-updates-draft-only', f.state().get('draft_text') == edited and f.first_path.read_text() == before)
    f.check('unsaved-draft-does-not-replace-effective-body', f.state()['effective_text'] == source)
    requests = len(f.server.bodies)
    f.check('global-f9-rejects-unsaved-real-ui-draft', f.command('command')[0] == 1)
    f.command('command-release')
    f.check('draft-f9-does-not-start-capture-or-model', not f.state()['busy'] and len(f.server.bodies) == requests)
    f.check('global-input-rejects-unsaved-real-ui-draft', f.command('insert-current')[0] == 1 and f.first_path.read_text() == before)
    f.photo('focus-02-unsaved-editor')
    pointer.click('完成编辑')
    saved = f.wait(lambda: s if (s := f.state())['effective_text'] == edited else None)
    f.check('real-editor-save-retains-original-asr', saved['raw'] == source)
    f.photo('focus-03-saved-body')

    # Explicitly return focus to the original external control. This must not
    # silently change the stored result destination to the overlay or a peer.
    f.focus(f.first)
    f.key('ctrl End')
    f.set_transcripts('Make the requested revision.')
    index = f.plan('success', output=revised)
    try:
        f.begin('command')
    except Exception:
        f.photo('primary-f9-after-edit-failure')
        (f.evidence/'primary-f9-after-edit-failure.json').write_text(json.dumps(
            {'state':f.state(),'widgets':f.accessible()},ensure_ascii=False,indent=2))
        raise
    f.command('command-release')
    changed = f.phase('ready')
    body = json.loads(json.loads(f.server.bodies[index])['messages'][1]['content'])
    f.check('f9-after-real-overlay-edit-uses-saved-body', body['original_body'] == edited and changed['effective_text'] == revised)
    f.check('focus-return-revision-does-not-auto-input', f.first_path.read_text() == before and changed['manual_confirmation'])
    f.command('undo-revision')
    f.check('undo-restores-real-editor-saved-body', f.state()['effective_text'] == edited)
    f.photo('focus-04-revised-and-undone')

    other, other_path = f.editor('Other independent window.', 'focus-other')
    f.check('different-window-cannot-revise-or-steal-target', f.command('command')[0] == 1 and f.state()['effective_text'] == edited)
    f.command('command-release')
    f.check('different-window-cannot-insert', f.command('insert-current')[0] == 1 and other_path.read_text() == 'Other independent window.')
    f.focus(f.first)
    f.key('ctrl End')
    # Validate a second safe return rather than accepting failure forever after
    # focus briefly moved away. The UI button and global key share the endpoint.
    f.set_transcripts('Keep the result as requested.')
    f.plan('success', output=revised)
    pointer.click('语音修改（F9）')
    source_port, target_port = f.wait(f.ports, seconds=4)
    f.run('pw-link', source_port, target_port)
    f.phase('recording'); time.sleep(.2)
    f.command('stop'); f.phase('ready')
    f.check('return-from-other-window-can-revise-original', f.state()['effective_text'] == revised)
    pointer.click('确认输入')
    f.phase('idle')
    f.wait(lambda: f.first_path.read_text() == before + revised)
    f.check('final-explicit-ui-input-reaches-original-control-once', f.first_path.read_text() == before + revised and other_path.read_text() == 'Other independent window.' and f.command('insert-current')[0] == 1)
    f.check('no-extra-model-calls', f.server.unexpected == 0)
    f.photo('focus-05-original-target-confirmed')
