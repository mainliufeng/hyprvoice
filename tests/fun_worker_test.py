#!/usr/bin/env python3
"""Child-process protocol tests; real model/desktop acceptance runs separately."""
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import textwrap
import unittest
import wave

BINARY = Path(sys.argv.pop(1)).resolve()


class WorkerContract(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="hv-fun-contract-")
        self.root = Path(self.temp.name)
        self.env = dict(os.environ, HYPRVOICE_CONFIG=str(self.root / "config.json"), TMPDIR=str(self.root))
        for name in ("funasr-encoder-f16.gguf", "qwen3-0.6b-q8_0.gguf", "fsmn-vad.gguf"):
            (self.root / name).touch()
        self.worker = self.root / "worker"
        config = {"asr": {"backend": "fun"}, "fun": {"worker": str(self.worker), "model_dir": str(self.root), "timeout_seconds": 5}}
        (self.root / "config.json").write_text(json.dumps(config))
        self.audio = self.root / "audio.wav"
        with wave.open(str(self.audio), "wb") as wav:
            wav.setparams((1, 2, 16000, 0, "NONE", "NONE"))
            wav.writeframes(b"\x00\x00" * 1600)

    def tearDown(self):
        self.temp.cleanup()

    def program(self, body):
        self.worker.write_text("#!/usr/bin/env python3\nimport json, sys, time, wave\n" + textwrap.dedent(body))
        self.worker.chmod(0o755)

    def run_model(self):
        result = subprocess.run([str(BINARY), "transcribe", str(self.audio)], env=self.env,
                                capture_output=True, text=True, timeout=12)
        self.assertEqual(list(self.root.glob("hyprvoice-fun-*")), [], "temporary recording leaked")
        return result

    def test_float_recording_and_response(self):
        self.program('''
            print('{"ready":true}', flush=True)
            for line in sys.stdin:
                audio = open(json.loads(line)['audio'], 'rb').read()
                assert audio[:4] == b'RIFF' and audio[20:22] == b'\\x03\\x00'
                assert len(audio) == 44 + 1600 * 4
                print(json.dumps({'text': '测试 Python', 'speech': True}), flush=True)
        ''')
        result = self.run_model()
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(json.loads(result.stdout)["text"], "测试 Python")
        self.assertEqual(json.loads(result.stdout)["streaming"], "")

    def test_crash_does_not_fallback_or_insert(self):
        self.program('''
            print('{"ready":true}', flush=True)
            sys.stdin.readline()
            sys.exit(3)
        ''')
        result = self.run_model()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("exited unexpectedly", result.stderr)
        self.assertEqual(result.stdout, "")

    def test_timeout_kills_child_and_removes_recording(self):
        self.program('''
            print('{"ready":true}', flush=True)
            sys.stdin.readline()
            time.sleep(30)
        ''')
        result = self.run_model()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("timed out", result.stderr)

    def test_initialization_error_is_reported(self):
        self.program("print('{\"error\":\"broken test model\"}', flush=True)")
        result = self.run_model()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("broken test model", result.stderr)


if __name__ == "__main__":
    unittest.main()
