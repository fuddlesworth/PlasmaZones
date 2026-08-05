#!/bin/bash
# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Launch a nested kwin_wayland session with virtual outputs and the KWin
# effect loaded from the BUILD TREE. Reproduces multi-monitor compositor
# bugs (straddling, parking, mixed scale) in minutes with no logout: the
# effect .so never reloads in a live session, but a nested compositor
# starts fresh every time.
#
# Usage:
#   scripts/nested-kwin/run-nested.sh [output-count]
#
# State (isolated XDG home, env.sh, logs) lives in $PZ_NESTED_DIR
# (default /tmp/pz-nested-$USER). After it is up, source env.sh from
# there in every follow-up shell:
#   . /tmp/pz-nested-$USER/env.sh
# then start the daemon with daemon.sh, and inspect with dump-windows.sh
# and capture-output.py.
#
# Gotchas learned the hard way:
#   - fish cannot source env.sh (POSIX `export VAR=...`); use bash -c.
#   - Do not `pkill -f` a pattern that appears in your own command line.
#   - The stock session bus D-Bus-activates the INSTALLED daemon the
#     moment the effect connects; daemon.sh kills it and claims the name
#     with the build-tree daemon.
#   - Screenshots are NOT evidence of effect behaviour: ScreenShot2's
#     CaptureScreen bypasses the effect chain entirely, and workspace
#     captures run it with no output pass. Only committed geometry
#     (dump-windows.sh) and journal diagnostics are trustworthy.
set -eu
OUTPUTS="${1:-2}"
REPO="$(cd "$(dirname "$0")/../.." && pwd)"
NEST="${PZ_NESTED_DIR:-/tmp/pz-nested-$USER}"
HOME_N="$NEST/home"
rm -rf "$HOME_N"
mkdir -p "$HOME_N/config" "$HOME_N/data" "$HOME_N/cache" "$HOME_N/state"

cat > "$HOME_N/config/kwinrc" <<KWINRC
[Plugins]
kwin_effect_plasmazonesEnabled=true
KWINRC

export XDG_CONFIG_HOME="$HOME_N/config"
export XDG_DATA_HOME="$HOME_N/data"
export XDG_CACHE_HOME="$HOME_N/cache"
export XDG_STATE_HOME="$HOME_N/state"
export QT_QPA_PLATFORM=wayland
export QT_PLUGIN_PATH="$REPO/build/bin:${QT_PLUGIN_PATH:-}"
export KWIN_SCREENSHOT_NO_PERMISSION_CHECKS=1
export QT_LOGGING_RULES="plasmazones.*=true"

exec dbus-run-session -- sh -c "
  {
    echo \"export DBUS_SESSION_BUS_ADDRESS='\$DBUS_SESSION_BUS_ADDRESS'\"
    echo \"export WAYLAND_DISPLAY=pznested\"
    echo \"export XDG_CONFIG_HOME='$XDG_CONFIG_HOME'\"
    echo \"export XDG_DATA_HOME='$XDG_DATA_HOME'\"
    echo \"export XDG_CACHE_HOME='$XDG_CACHE_HOME'\"
    echo \"export XDG_STATE_HOME='$XDG_STATE_HOME'\"
    echo \"export QT_QPA_PLATFORM=wayland\"
    echo \"export QT_PLUGIN_PATH='$QT_PLUGIN_PATH'\"
    echo \"export KWIN_SCREENSHOT_NO_PERMISSION_CHECKS=1\"
    echo \"export QT_LOGGING_RULES='plasmazones.*=true'\"
    echo \"export PZ_NESTED_DIR='$NEST'\"
    echo \"export PZ_NESTED_REPO='$REPO'\"
  } > '$NEST/env.sh'
  exec kwin_wayland --virtual --output-count $OUTPUTS --socket pznested --no-lockscreen --no-global-shortcuts --no-kactivities
"
