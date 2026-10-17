import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
import score


class PerCaseRegressionTests(unittest.TestCase):
    def setUp(self):
        self.cases = {
            'improvement': {'id': 'improvement', 'reference': 'the amount is 2400',
                            'fold': 'test', 'category': 'speech'},
            'regression': {'id': 'regression', 'reference': 'do not send',
                           'fold': 'test', 'category': 'speech'},
        }
        self.baseline = {'improvement': {'id': 'improvement', 'text': 'wrong'},
                         'regression': {'id': 'regression', 'text': 'do not send'}}
        self.candidate = {'improvement': {'id': 'improvement', 'text': 'the amount is 2400'},
                          'regression': {'id': 'regression', 'text': 'do send'}}

    def test_improved_total_does_not_hide_one_regression(self):
        before = score.evaluate(self.cases, self.baseline)['test/speech']['edits']
        after = score.evaluate(self.cases, self.candidate)['test/speech']['edits']
        self.assertLess(after, before)
        comparisons = score.compare_cases(self.cases, self.baseline, self.candidate)
        self.assertEqual([r['id'] for r in comparisons if r['delta'] > 0], ['regression'])

    def test_missing_id_and_new_error(self):
        with self.assertRaises(ValueError):
            score.compare_cases(self.cases, self.baseline, {})
        candidate = dict(self.baseline)
        candidate['regression'] = dict(self.baseline['regression'], error='synthetic failure')
        row = score.compare_cases(self.cases, self.baseline, candidate)[1]
        self.assertEqual(row['delta'], 0)
        self.assertTrue(row['new_error'])

    def test_cli_explicit_gate_fails_then_accepts_nonregression(self):
        with tempfile.TemporaryDirectory(prefix='hyprvoice-score-') as folder:
            root = Path(folder)
            def write(name, records):
                path = root / name
                path.write_text(''.join(json.dumps(r) + '\n' for r in records.values()))
                return path
            manifest = write('manifest.jsonl', self.cases)
            before = write('baseline.jsonl', self.baseline)
            after = write('candidate.jsonl', self.candidate)
            command = [sys.executable, str(Path(score.__file__)), str(manifest), str(after),
                       '--baseline', str(before), '--fail-on-regression']
            failed = subprocess.run(command, capture_output=True, text=True)
            self.assertEqual(failed.returncode, 1)
            self.assertEqual(json.loads(failed.stdout)['regressions'][0]['id'], 'regression')
            write('candidate.jsonl', self.baseline)
            passed = subprocess.run(command, capture_output=True, text=True)
            self.assertEqual(passed.returncode, 0)
            self.assertEqual(json.loads(passed.stdout)['regressions'], [])
            missing = subprocess.run(command[:4] + ['--fail-on-regression'],
                                     capture_output=True, text=True)
            self.assertEqual(missing.returncode, 2)


if __name__ == '__main__':
    unittest.main()
