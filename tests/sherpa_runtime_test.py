"""Reject a replaced ASR library before passing it any model config structs."""
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

BINARY = Path(sys.argv.pop(1)).resolve()
COMPILER = sys.argv.pop(1)


class RuntimeCompatibility(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="hv-sherpa-abi-")
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        source = self.root / "replacement.cpp"
        source.write_text('''
#include <cstdlib>
extern "C" const char *SherpaOnnxGetVersionStr() {
  return std::getenv("HV_TEST_NULL_VERSION") ? nullptr : "0.0.0";
}
extern "C" void *SherpaOnnxCreateOnlineRecognizer(const void *) {
  std::abort(); // An incompatible library must never receive a config struct.
}
''')
        self.shim = self.root / "replacement.so"
        subprocess.run([COMPILER, "-shared", "-fPIC", str(source), "-o", str(self.shim)],
                       check=True, capture_output=True)
        self.config = self.root / "config.json"
        self.config.write_text(json.dumps({"hotwords": "private-hotword-canary"}))
        self.env = dict(os.environ, HYPRVOICE_CONFIG=str(self.config),
                        LD_PRELOAD=str(self.shim))

    def reject(self, env):
        result = subprocess.run([str(BINARY), "transcribe", "missing.wav"],
                                env=env, text=True, capture_output=True, timeout=5)
        self.assertEqual(result.returncode, 1)
        self.assertEqual(result.stdout, "")
        self.assertIn("本地语音库版本已变化", result.stderr)
        self.assertIn("重新构建并安装", result.stderr)
        self.assertNotIn("private-hotword-canary", result.stderr)

    def test_replaced_library_rejected_before_assets_or_recognizer(self):
        self.reject(self.env)

    def test_missing_version_rejected(self):
        self.reject(dict(self.env, HV_TEST_NULL_VERSION="1"))

    def test_version_command_still_available_for_repair(self):
        result = subprocess.run([str(BINARY), "--version"], env=self.env,
                                text=True, capture_output=True, timeout=5)
        self.assertEqual(result.returncode, 0)
        self.assertTrue(result.stdout.startswith("hyprvoice "))
        # `--version` is the first command run after an upgrade; it must name a
        # replaced library instead of printing a version that looks healthy.
        self.assertIn("语音库：与构建时不一致", result.stdout)
        self.assertIn("重新构建并安装", result.stdout)
        self.assertNotIn("private-hotword-canary", result.stdout)

    def test_version_with_matching_library_keeps_one_line(self):
        env = {k: v for k, v in os.environ.items()
               if k not in ("LD_PRELOAD", "HV_TEST_NULL_VERSION")}
        result = subprocess.run([str(BINARY), "--version"], env=env, text=True,
                                capture_output=True, timeout=5)
        self.assertEqual(result.returncode, 0)
        self.assertRegex(result.stdout, r"^hyprvoice \S+\n$")


unittest.main()
