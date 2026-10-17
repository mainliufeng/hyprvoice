#!/usr/bin/env python3
"""Actual local X-ASR on one frozen public sample; no desktop, mic or LLM."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--candidate', type=Path, required=True)
parser.add_argument('--corpus-root', type=Path, required=True)
parser.add_argument('--model-config', type=Path, required=True)
parser.add_argument('--baseline', type=Path)
parser.add_argument('--output', type=Path, required=True)
args = parser.parse_args()
repo = args.candidate.resolve()
binary = repo / 'build/hyprvoice'
assert binary.is_file(), 'Build the actual candidate first'
assert args.output.resolve() != repo, 'Use a dedicated evidence directory'
args.output.mkdir(parents=True, exist_ok=True)
manifest = repo / 'tests/fixtures/manifest.jsonl'
sample = next(json.loads(row) for row in manifest.read_text().splitlines()
              if json.loads(row)['id'] == 'long-en_us-dev')
expected_audio = '9b2772785204af4164f15d08c7871b1bc39e64a5c9193a4b7d689c71094510ee'
audio = args.corpus_root.resolve() / 'long-en_us-dev.wav'
assert sample['sha256'] == expected_audio
assert hashlib.sha256(audio.read_bytes()).hexdigest() == expected_audio, 'Frozen public audio changed'
sample['audio'] = str(audio)
local_manifest = args.output / 'tail-manifest.jsonl'
local_manifest.write_text(json.dumps(sample, ensure_ascii=False) + '\n')
# Only existing public model assets are needed. Exclude personal hotwords,
# cloud credentials, microphone settings and application context entirely.
settings = json.loads(args.model_config.read_text())
config = {key: settings[key] for key in ('streaming_model', 'offline_model', 'vad_model')}
config.update({'asr': {'backend': 'x-asr'}, 'hotwords': '', 'scene': 'raw',
               'context': {'enabled': False}})
model_hashes = {}
for key, value in list(config.items()):
    if key not in ('streaming_model', 'offline_model', 'vad_model'):
        continue
    model = Path(value).expanduser().resolve()
    assert model.exists(), f'Existing local asset missing: {model}'
    config[key] = str(model)
    files = [model] if model.is_file() else sorted(model.glob('*.onnx')) + sorted(model.glob('tokens.txt'))
    for path in files:
        model_hashes[key + '/' + path.name] = hashlib.sha256(path.read_bytes()).hexdigest()
config_path = args.output / 'local-asr-config.json'
config_path.write_text(json.dumps(config, ensure_ascii=False))
env = dict(os.environ, HYPRVOICE_CONFIG=str(config_path.resolve()))
result_path = args.output / 'tail-results.jsonl'
with result_path.open('w') as output, (args.output / 'replay.log').open('w') as log:
    subprocess.run([str(binary), 'replay', str(local_manifest.resolve())], env=env,
                   stdout=output, stderr=log, check=True, timeout=120)
result = json.loads(result_path.read_text())
assert result['id'] == 'long-en_us-dev'
complete_tail = 'The major religion in Moldova is Orthodox Christian.'
provenance_file = repo / 'snapshot-provenance.json'
if provenance_file.exists():
    provenance = json.loads(provenance_file.read_text())
    source_head, source_status = provenance['source_head'], provenance['source_status']
    assert provenance['binary_sha256'] == hashlib.sha256(binary.read_bytes()).hexdigest()
else:
    source_head = subprocess.check_output(['git', '-C', str(repo), 'rev-parse', 'HEAD'], text=True).strip()
    source_status = subprocess.check_output(['git', '-C', str(repo), 'status', '--porcelain'], text=True).strip()
report = {'sample_id': result['id'], 'audio_sha256': expected_audio,
          'binary_sha256': hashlib.sha256(binary.read_bytes()).hexdigest(),
          'model_sha256': model_hashes,
          'source_head': source_head, 'source_status': source_status,
          'frozen_snapshot': provenance_file.exists(),
          'tail_complete': result['text'].endswith(complete_tail),
          'warning': result.get('warning', ''),
          'auto_commit_blocked': result.get('auto_commit_blocked'),
          'real_local_asr': True, 'external_model_calls': False, 'real_microphone': False}
score_args = [sys.executable, str(repo / 'tests/score.py'), str(local_manifest.resolve()),
              str(result_path.resolve()), '--verify-audio', '--output', str((args.output / 'score.json').resolve())]
if args.baseline:
    baseline = json.loads(args.baseline.read_text())
    score_args += ['--baseline', str(args.baseline.resolve()), '--fail-on-regression']
    report['text_equal_to_baseline'] = result['text'] == baseline['text']
    marker = 'The major religion'
    report['prefix_equal_to_baseline'] = result['text'].split(marker)[0] == baseline['text'].split(marker)[0]
else:
    report['baseline_created_only'] = True
scored = subprocess.run(score_args, capture_output=True, text=True, timeout=20)
report['frozen_score_passed'] = scored.returncode == 0
(args.output / 'score.log').write_text(scored.stdout + scored.stderr)
(args.output / 'report.json').write_text(json.dumps(report, ensure_ascii=False, indent=2) + '\n')
print(json.dumps(report, ensure_ascii=False, indent=2))
if not report['tail_complete'] or scored.returncode:
    raise SystemExit(1)
