#!/usr/bin/env bash
set -euo pipefail
app_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
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
if [[ "$with_fun" == ON ]]; then
  python3 "$app_root/scripts/fun_models.py" --cache "$app_root/build/benchmark-20261004"
  install -Dm755 "$app_root/build/fun-worker" "$HOME/.local/libexec/hyprvoice/fun-worker"
  install -Dm644 "$app_root/build/Fun-ASR-LICENSE" "$HOME/.local/share/licenses/hyprvoice/Fun-ASR-LICENSE"
  install -Dm644 "$app_root/build/llama.cpp-LICENSE" "$HOME/.local/share/licenses/hyprvoice/llama.cpp-LICENSE"
fi
printf '%s\n' 'Installed binary and service. Configure models, then import Hyprland session variables and start the service as documented in README.md.'
