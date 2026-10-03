#!/usr/bin/env bash
set -euo pipefail
app_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
cmake -S "$app_root" -B "$app_root/build" -DCMAKE_BUILD_TYPE=Release
cmake --build "$app_root/build" -j2
ctest --test-dir "$app_root/build" --output-on-failure
install -Dm755 "$app_root/build/hyprvoice" "$HOME/.local/bin/hyprvoice"
install -Dm644 "$app_root/config/hyprvoice.service" "$HOME/.config/systemd/user/hyprvoice.service"
printf '%s\n' 'Installed binary and service. Configure models, then import Hyprland session variables and start the service as documented in README.md.'
