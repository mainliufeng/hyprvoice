"""Exercise desktop target selection without a compositor or AT-SPI bus."""

import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest


class DesktopTargetTest(unittest.TestCase):
    def capture(self, capabilities, window):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            config = root / "config.json"
            config.write_text("{}", encoding="utf-8")
            hyprctl = root / "hyprctl"
            hyprctl.write_text(
                """#!/bin/sh
case "$*" in
  '-j seat capabilities') printf '%s\\n' "$TEST_CAPABILITIES" ;;
  'activewindow -j') printf '%s\\n' "$TEST_WINDOW" ;;
  *) exit 2 ;;
esac
""",
                encoding="utf-8",
            )
            hyprctl.chmod(0o700)
            return subprocess.run(
                [PROBE, str(config), "capture", str(root / "target.json")],
                env=dict(
                    os.environ,
                    PATH=directory + os.pathsep + os.environ.get("PATH", ""),
                    AT_SPI_BUS_ADDRESS=f"unix:path={root / 'no-a11y'}",
                    TEST_CAPABILITIES=capabilities,
                    TEST_WINDOW=json.dumps(window),
                ),
                capture_output=True,
                text=True,
                timeout=10,
            )

    def test_active_window_without_seat_capability(self):
        window = {"address": "0x1234", "class": "editor", "pid": 1234}
        for capabilities in ("unknown command", '{"features":[]}'):
            with self.subTest(capabilities=capabilities):
                result = self.capture(capabilities, window)
                self.assertEqual(result.returncode, 0, result.stderr)
                target = json.loads(result.stdout)
                expected = {
                    "address": window["address"],
                    "app": window["class"],
                    "pid": window["pid"],
                    "kind": "application",
                    "seat": "",
                    "token": "",
                }
                self.assertEqual({key: target[key] for key in expected}, expected)

    def test_no_active_window_without_seat_capability(self):
        for window in ({}, None):
            with self.subTest(window=window):
                result = self.capture("unknown command", window)
                self.assertEqual(result.returncode, 1, result.stderr)
                self.assertIn("No active application window", result.stderr)


if __name__ == "__main__":
    PROBE = sys.argv.pop(1)
    unittest.main()
