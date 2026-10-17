#!/usr/bin/env bash
# No daily sockets, audio, input/card devices, HOME, system bus or network.
set -euo pipefail
repo=$(cd "$(dirname "$0")/.." && pwd)
: "${HV_TEST_HYPRLAND:?Select an existing Hyprland executable}"
: "${HV_TEST_EVIDENCE:?Select an empty evidence directory}"
mkdir -p "$HV_TEST_EVIDENCE"
artifacts=$(mktemp -d /tmp/hv-revision.XXXXXX)
mkdir -p "$artifacts/home" "$artifacts/tmp"
echo "Private runtime artifacts: $artifacts"
exec /usr/bin/python3 -c 'import os,sys; os.closerange(3,1048576); os.execv(sys.argv[1],sys.argv[1:])' \
 /usr/bin/bwrap --unshare-all --die-with-parent --new-session \
 --ro-bind / / --dev /dev --proc /proc --tmpfs /run --tmpfs /tmp --tmpfs /var/tmp --tmpfs /home --tmpfs /root --tmpfs /opt \
 --dir /dev/dri --dev-bind /dev/dri/renderD128 /dev/dri/renderD128 \
 --ro-bind "$repo" /opt/hv --ro-bind "$HV_TEST_HYPRLAND" /opt/Hyprland \
 --bind "$artifacts" /tmp --bind "$HV_TEST_EVIDENCE" /tmp/evidence \
 --clearenv --setenv PATH /usr/bin:/bin --setenv LANG C.UTF-8 \
 --setenv HOME /tmp/home --setenv TMPDIR /tmp/tmp --setenv USER "$(id -un)" \
 --setenv XDG_CONFIG_HOME /tmp/home/.config --setenv XDG_DATA_HOME /tmp/home/.local/share \
 --setenv XDG_STATE_HOME /tmp/home/.local/state --setenv HV_PRIVATE_DESKTOP 1 --setenv HV_TEST_SUITE "${HV_TEST_SUITE:-revision}" \
 --setenv GDK_BACKEND wayland --setenv GTK_A11Y atspi --setenv GTK_IM_MODULE wayland \
 --setenv AQ_DRM_DEVICES /dev/null --setenv LIBSEAT_BACKEND noop \
 --setenv LP_NUM_THREADS 1 --setenv OMP_NUM_THREADS 1 \
 --setenv DBUS_SYSTEM_BUS_ADDRESS unix:path=/run/no-system-bus \
 /usr/bin/python3 /opt/hv/tests/private_desktop_runner.py
