import unittest

from benchmark_score import evaluate, tokens


class BenchmarkScoreTests(unittest.TestCase):
    def test_mixed_tokens_and_contractions(self):
        self.assertEqual(tokens("我用 DeepSeek，don't 改！１２３"),
                         ["我", "用", "deepseek", "don't", "改", "123"])

    def test_word_vs_character_edits_and_failure_accounting(self):
        cases = {"a": {"reference": "test one", "category": "en", "duration_seconds": 2},
                 "b": {"reference": "", "category": "silence", "duration_seconds": 1}}
        results = {"a": {"text": "test two", "asr_ms": 500, "llm_called": True,
                         "rewrite_ms": 1000, "rewrite_error": "timeout",
                         "auto_commit_blocked": True}, "b": {"text": "noise"}}
        groups = evaluate(cases, results)["summary"]
        self.assertEqual(groups["en"]["token_edits"], 1)
        self.assertEqual(groups["en"]["character_edits"], 3)
        self.assertEqual(groups["en"]["mer"], .5)
        self.assertEqual(groups["en"]["compute_rtf"], .25)
        self.assertEqual(groups["en"]["auto_commit_blocked"], 1)
        self.assertEqual(groups["en"]["rewrite_p50_ms"], 1000)
        self.assertIsNone(groups["silence"]["cer"])
        self.assertEqual(groups["silence"]["false_insertions"], 1)

    def test_incomplete_run_is_not_a_score(self):
        with self.assertRaises(ValueError):
            evaluate({"a": {"reference": "a", "category": "en"}}, {})

    def test_missing_timing_is_unknown(self):
        groups = evaluate({"a": {"reference": "a", "category": "en", "duration_seconds": 1}},
                          {"a": {"text": "a"}})["summary"]
        self.assertIsNone(groups["en"]["compute_rtf"])
        self.assertIsNone(groups["en"]["asr_p50_ms"])

    def test_punctuation_only_is_still_an_insertion_on_silence(self):
        groups = evaluate({"a": {"reference": "", "category": "silence"}},
                          {"a": {"text": "。。。"}})["summary"]
        self.assertEqual(groups["silence"]["false_insertions"], 1)


if __name__ == "__main__":
    unittest.main()
