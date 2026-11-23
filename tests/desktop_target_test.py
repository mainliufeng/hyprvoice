"""Check production Desktop targeting with synthetic hyprctl responses."""
import json
import os
from pathlib import Path
import subprocess
import sys
from tempfile import TemporaryDirectory

with TemporaryDirectory(prefix="hv-desktop-target-") as directory:
    root = Path(directory)
    config = root / "config.json"
    config.write_text("{}")
    command = root / "hyprctl"
    command.write_text("""#!/bin/sh
case "$*" in
  '-j seat capabilities')
    if [ "$HV_SCENARIO" = patched ] || [ "$HV_SCENARIO" = denied ] || [ "$HV_SCENARIO" = malformed ]; then
      echo '{"features":["physical-input-target-v1"]}'
      exit 0
    fi
    [ "$HV_SCENARIO" != stock_no_feature ] || { echo '{"features":[]}'; exit 0; }
    exit 1 ;;
  'activewindow -j')
    if [ "$HV_SCENARIO" = unfocused ]; then echo null; exit 0; fi
    if [ "$HV_SCENARIO" = unfocused_empty ]; then echo '{}'; exit 0; fi
    [ "$HV_SCENARIO" = stock ] || [ "$HV_SCENARIO" = stock_no_feature ] || exit 2
    echo '{"address":"0x100","class":"synthetic-editor","pid":42}' ;;
  '-j seat input-target')
    if [ "$HV_SCENARIO" = denied ]; then echo '{"allowed":false,"reason":"fixture denied"}'; exit 0; fi
    if [ "$HV_SCENARIO" = malformed ]; then echo 'null'; exit 0; fi
    [ "$HV_SCENARIO" = patched ] || [ "$HV_SCENARIO" = denied ] || [ "$HV_SCENARIO" = malformed ] || exit 2
    echo '{"allowed":true,"kind":"application","seatName":"Agent","token":"fixture","window":{"address":"0x200","class":"synthetic-editor","pid":84}}' ;;
  *) exit 2 ;;
esac
""")
    command.chmod(0o700)

    def run(scenario):
        return subprocess.run(
            [sys.argv[1], str(config), "target"],
            env=dict(os.environ, PATH=str(root), HV_SCENARIO=scenario),
            text=True,
            capture_output=True,
            timeout=5,
        )

    for scenario in ("stock", "stock_no_feature"):
        stock = run(scenario)
        assert stock.returncode == 0, stock.stderr
        target = json.loads(stock.stdout)
        expected = {"address": "0x100", "app": "synthetic-editor", "pid": 42,
                    "kind": "application", "seat": "", "token": ""}
        assert {key: target[key] for key in expected} == expected

    for scenario in ("unfocused", "unfocused_empty"):
        unfocused = run(scenario)
        assert unfocused.returncode == 1, unfocused.stderr
        assert "No active application window" in unfocused.stderr

    patched = run("patched")
    assert patched.returncode == 0, patched.stderr
    assert json.loads(patched.stdout)["address"] == "0x200"
    assert json.loads(patched.stdout)["token"] == "fixture"

    denied = run("denied")
    assert denied.returncode == 1 and "fixture denied" in denied.stderr, denied.stderr
    malformed = run("malformed")
    assert malformed.returncode == 1 and "Invalid desktop input route" in malformed.stderr, malformed.stderr
    print("PASS stock fallback, stock no-window, patched route, denied route, malformed route")
