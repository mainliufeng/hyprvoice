#!/usr/bin/env python3
"""Score real context_probe outputs against public manually authored cases."""
import json
import sys
import unicodedata
from pathlib import Path


def normalized(text):
    return ''.join(c.casefold() for c in text if not c.isspace() and not unicodedata.category(c).startswith('P'))


cases = {x['id']: x for line in Path(sys.argv[1]).read_text().splitlines() if (x := json.loads(line))}
rows = [json.loads(line) for line in Path(sys.argv[2]).read_text().splitlines()]
assert len(rows) == len(cases) and {x['id'] for x in rows} == set(cases), 'missing/duplicate cases'
result = {'cases': len(cases), 'without_context_passed': 0, 'with_context_passed': 0, 'rows': []}
for row in rows:
    test = cases[row['id']]
    detail = {'id': row['id'], 'category': test['category']}
    expected = {normalized(x) for x in test['expected']}
    for mode in ('without_context', 'with_context'):
        detail[mode] = row.get(mode)
        detail[mode + '_pass'] = normalized(row.get(mode, '')) in expected
        result[mode + '_passed'] += int(detail[mode + '_pass'])
        if mode + '_error' in row:
            detail[mode + '_error'] = row[mode + '_error']
    result['rows'].append(detail)
print(json.dumps(result, ensure_ascii=False, indent=2))
