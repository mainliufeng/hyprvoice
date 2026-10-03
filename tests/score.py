#!/usr/bin/env python3
"""Standalone CER and speech/no-speech regression scoring; standard library only."""
import argparse
import hashlib
import json
import unicodedata
from collections import defaultdict
from pathlib import Path


def normalized(text):
    return ''.join(c for c in unicodedata.normalize('NFKC', text).casefold() if c.isalnum())


def distance(expected, actual):
    previous = list(range(len(actual) + 1))
    for row, letter in enumerate(expected, 1):
        current = [row]
        for column, other in enumerate(actual, 1):
            current.append(min(current[-1] + 1, previous[column] + 1,
                               previous[column - 1] + (letter != other)))
        previous = current
    return previous[-1]


def records(path):
    found = {}
    for line in path.read_text().splitlines():
        if not line.strip():
            continue
        item = json.loads(line)
        if item['id'] in found:
            raise ValueError('duplicate case: ' + item['id'])
        found[item['id']] = item
    return found


def evaluate(cases, results):
    if cases.keys() != results.keys():
        raise ValueError('result IDs do not match the complete manifest')
    groups = defaultdict(lambda: dict(cases=0, characters=0, edits=0,
                                      false_insertions=0, errors=0))
    for case_id, case in cases.items():
        result = results[case_id]
        group = groups[case['fold'] + '/' + case['category']]
        expected = normalized(case['reference'])
        actual = normalized(result.get('text', ''))
        group['cases'] += 1
        group['characters'] += len(expected)
        group['edits'] += distance(expected, actual)
        group['false_insertions'] += not expected and bool(result.get('text', '').strip())
        group['errors'] += 'error' in result
    for group in groups.values():
        group['cer'] = group['edits'] / group['characters'] if group['characters'] else None
    return dict(groups)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('manifest', type=Path)
    parser.add_argument('results', type=Path)
    parser.add_argument('--baseline', type=Path)
    parser.add_argument('--verify-audio', action='store_true')
    parser.add_argument('--output', type=Path)
    args = parser.parse_args()
    cases, results = records(args.manifest), records(args.results)
    if args.verify_audio:
        for case in cases.values():
            path = args.manifest.parent / case['audio']
            if hashlib.sha256(path.read_bytes()).hexdigest() != case['sha256']:
                raise ValueError('audio hash mismatch: ' + case['id'])
    report = {'normalization': 'NFKC, casefold, Unicode alphanumeric characters',
              'summary': evaluate(cases, results)}
    if args.baseline:
        baseline = records(args.baseline)
        report['baseline_summary'] = evaluate(cases, baseline)
        report['changed_cases'] = [key for key in cases if results[key].get('text') != baseline[key].get('text')]
        report['identical_cases'] = len(cases) - len(report['changed_cases'])
    content = json.dumps(report, ensure_ascii=False, indent=2) + '\n'
    if args.output:
        args.output.write_text(content)
    else:
        print(content, end='')
    raise SystemExit(1 if any('error' in row for row in results.values()) else 0)
