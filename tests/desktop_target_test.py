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
    if [ "$HV_SCENARIO" = patched ]; then
      echo '{"features":["physical-input-target-v1"]}'
      exit 0
    fi
    exit 1 ;;
  'activewindow -j')
    if [ "$HV_SCENARIO" = unfocused ]; then echo null; exit 0; fi
    [ "$HV_SCENARIO" = stock ] || exit 2
    echo '{"address":"0x100","class":"synthetic-editor","pid":42}' ;;
  '-j seat input-target')
    [ "$HV_SCENARIO" = patched ] || exit 2
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

    stock = run("stock")
    assert stock.returncode == 0, stock.stderr
    assert json.loads(stock.stdout)["address"] == "0x100"
    assert not json.loads(stock.stdout)["token"]

    unfocused = run("unfocused")
    assert unfocused.returncode == 1
    assert "No active application window" in unfocused.stderr

    patched = run("patched")
    assert patched.returncode == 0, patched.stderr
    assert json.loads(patched.stdout)["address"] == "0x200"
    assert json.loads(patched.stdout)["token"] == "fixture"
