#!/usr/bin/env bash
set -euo pipefail
app_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
# Name the source this build came from. The 2026-11-18 install on this host was
# built from a checkout one commit behind main and nothing said so, so the
# installed 0.3.1 lacked that day's `diagnose` repair. Only a git checkout can
# say this; a source tarball has no .git and prints "commit unknown".
if git -C "$app_root" rev-parse --git-dir >/dev/null 2>&1; then
  source_commit="$(git -C "$app_root" rev-parse --short HEAD 2>/dev/null || true)"
  printf 'install: building source commit %s\n' "${source_commit:-unknown}"
  upstream_ref="$(git -C "$app_root" rev-parse --abbrev-ref --symbolic-full-name '@{u}' 2>/dev/null || true)"
  if [[ -n "$upstream_ref" ]]; then
    behind_count="$(git -C "$app_root" rev-list --count "HEAD..$upstream_ref" 2>/dev/null || true)"
    if [[ -n "$behind_count" && "$behind_count" != 0 ]]; then
      printf 'install: 本检出落后 %s %s 个提交（按上次 fetch 记录的引用判断，未联网核对）。\n' "$upstream_ref" "$behind_count" >&2
      printf 'install: 如果不是有意构建旧版本，先 git fetch && git pull --ff-only 再重跑本脚本；只重建旧检出会漏掉已合入的修复。\n' >&2
    fi
  fi
else
  printf '%s\n' 'install: source is not a git checkout, commit unknown'
fi
with_fun=OFF
if [[ "${1:-}" == --with-fun ]]; then
  with_fun=ON
elif [[ $# -gt 0 ]]; then
  printf '%s\n' 'Usage: install.sh [--with-fun]' >&2
  exit 2
fi
cmake -S "$app_root" -B "$app_root/build" -DCMAKE_BUILD_TYPE=Release -DHYPRVOICE_WITH_FUN_ASR="$with_fun"
cmake --build "$app_root/build" -j2
ctest --test-dir "$app_root/build" --output-on-failure
install -Dm755 "$app_root/build/hyprvoice" "$HOME/.local/bin/hyprvoice"
install -Dm644 "$app_root/config/hyprvoice.service" "$HOME/.config/systemd/user/hyprvoice.service"
desktop_entry=$(mktemp)
trap 'rm -f -- "$desktop_entry"' EXIT
# Graphical launchers may omit ~/.local/bin from PATH. Quote the exact path
# for Exec, then escape it again for the Desktop Entry string format.
python3 - "$app_root/config/hyprvoice.desktop" "$HOME/.local/bin/hyprvoice" >"$desktop_entry" <<'PY'
from pathlib import Path
import sys

template = Path(sys.argv[1]).read_text(encoding="utf-8")

def desktop_arg(value):
    value = value.replace("%", "%%")
    quoted = '"' + "".join("\\" + char if char in '\\"`$' else char for char in value) + '"'
    return (quoted.replace("\\", "\\\\").replace("\n", "\\n")
            .replace("\r", "\\r").replace("\t", "\\t"))

executable = sys.argv[2]
command = desktop_arg(executable)
if "%" in executable or "=" in executable:
    # Some launchers validate argv[0] before expanding %% and the spec bars
    # '=' in argv[0]. Pass these paths as an argument to a fixed exec script.
    exec_path = desktop_arg('exec "$1"')
    command = f"/bin/sh -c {exec_path} hyprvoice {command}"
print(template.replace("\nExec=hyprvoice\n", f"\nExec={command}\n", 1), end="")
PY
install -Dm644 "$desktop_entry" "${XDG_DATA_HOME:-$HOME/.local/share}/applications/hyprvoice.desktop"
if [[ "$with_fun" == ON ]]; then
  python3 "$app_root/scripts/fun_models.py" --cache "$app_root/build/benchmark-20261004"
  install -Dm755 "$app_root/build/fun-worker" "$HOME/.local/libexec/hyprvoice/fun-worker"
  install -Dm644 "$app_root/build/Fun-ASR-LICENSE" "$HOME/.local/share/licenses/hyprvoice/Fun-ASR-LICENSE"
  install -Dm644 "$app_root/build/llama.cpp-LICENSE" "$HOME/.local/share/licenses/hyprvoice/llama.cpp-LICENSE"
fi
# Report what landed. `--version` also names a replaced speech library, and it
# reads no config and starts no recording, so this stays cheap and side-effect
# free even on the install host.
printf '%s\n' 'install: installed binary reports:'
"$app_root/build/hyprvoice" --version || true
printf '%s\n' 'Installed HyprVoice, its application launcher, and the optional user service.'
printf '%s\n' 'Configure models, then open HyprVoice from the application launcher. See README.md for optional session autostart.'
