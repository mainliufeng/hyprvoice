#!/usr/bin/env bash
# Actual Cornice controlled-seat presentation; never bind host desktop/audio.
set -euo pipefail
owned_tests=$(cd "$(dirname "$0")" && pwd)
: "${HV_TEST_CANDIDATE:?Select a built candidate checkout}"
: "${HV_TEST_HYPRLAND_SOURCE:?Select the compatible built fork checkout}"
: "${HV_TEST_CORNICE_PRODUCT:?Select the existing installed Cornice product}"
: "${HV_TEST_CORNICE_SOURCE:?Select existing Cornice test harness sources}"
: "${HV_TEST_EVIDENCE:?Select a dedicated evidence directory}"
repo=$(realpath -e "$HV_TEST_CANDIDATE")
fork=$(realpath -e "$HV_TEST_HYPRLAND_SOURCE")
product=$(realpath -e "$HV_TEST_CORNICE_PRODUCT")
cornice_source=$(realpath -e "$HV_TEST_CORNICE_SOURCE")
[[ -x "$repo/build/hyprvoice" && -x "$fork/build-release/Hyprland" && -x "$product/bin/cornice-desktopd" ]]
[[ -f "$cornice_source/test/desktop_harness.py" && -f "$product/native/qml/Cornice/Desktop/qmldir" ]]
mkdir -p "$HV_TEST_EVIDENCE"
artifacts=$(mktemp -d /tmp/hv-native-focus.XXXXXX)
mkdir -p "$artifacts/home" "$artifacts/tmp"
echo "Private native runtime artifacts: $artifacts"
exec /usr/bin/python3 -c 'import os,sys; os.closerange(3,1048576); os.execv(sys.argv[1],sys.argv[1:])' \
 /usr/bin/bwrap --unshare-all --die-with-parent --new-session \
 --ro-bind / / --dev /dev --proc /proc --tmpfs /run --tmpfs /tmp --tmpfs /var/tmp --tmpfs /home --tmpfs /root --tmpfs /opt \
 --dir /dev/dri --dev-bind /dev/dri/renderD128 /dev/dri/renderD128 \
 --ro-bind "$repo" /opt/hv --ro-bind "$owned_tests" /opt/focus-tests \
 --ro-bind "$fork" /opt/fork --ro-bind "$product" /opt/cornice --ro-bind "$cornice_source" /opt/cornice-src \
 --bind "$artifacts" /tmp --bind "$HV_TEST_EVIDENCE" /tmp/evidence \
 --clearenv --setenv PATH /usr/bin:/bin --setenv LANG C.UTF-8 \
 --setenv HOME /tmp/home --setenv TMPDIR /tmp/tmp --setenv USER "$(id -un)" \
 --setenv XDG_CONFIG_HOME /tmp/home/.config --setenv XDG_DATA_HOME /tmp/home/.local/share \
 --setenv XDG_STATE_HOME /tmp/home/.local/state --setenv HV_PRIVATE_DESKTOP 1 \
 --setenv HV_NATIVE_ROUTE_ONLY "${HV_NATIVE_ROUTE_ONLY:-0}" --setenv HV_NATIVE_GEOMETRY_ONLY "${HV_NATIVE_GEOMETRY_ONLY:-0}" \
 --setenv CORNICE_TEST_SANDBOX 1 --setenv CORNICE_TEST_HYPRLAND_SOURCE /opt/fork \
 --setenv CORNICE_TEST_HYPRLAND /opt/fork/build-release/Hyprland --setenv CORNICE_TEST_PRODUCT /opt/cornice \
 --setenv CORNICE_PATH /opt/cornice --setenv GDK_BACKEND wayland --setenv GTK_A11Y atspi --setenv GTK_IM_MODULE wayland \
 --setenv AQ_DRM_DEVICES /dev/null --setenv LIBSEAT_BACKEND noop \
 --setenv LP_NUM_THREADS 1 --setenv OMP_NUM_THREADS 1 \
 --setenv DBUS_SYSTEM_BUS_ADDRESS unix:path=/run/no-system-bus \
 /usr/bin/python3 /opt/focus-tests/native_runner.py
