#!/usr/bin/env bash
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
#   scripts/nested-kwin/run-nested.sh [output-count] [width height] [scale]
#
# width/height/scale apply to EVERY virtual output (kwin_wayland's flags
# are global). Genuinely MIXED per-output scale needs a seeded
# kwinoutputconfig.json in the nested config home instead.
#
# State (isolated XDG home and env.sh) lives in $PZ_NESTED_DIR (default
# $XDG_RUNTIME_DIR/pz-nested, private to the user). The daemon's log is
# the only log file (daemon.sh writes it); the compositor's output stays
# on this terminal. After the session is up, source env.sh from there in
# every follow-up shell:
#   . "$XDG_RUNTIME_DIR/pz-nested/env.sh"
# then start the daemon with daemon.sh, and inspect with dump-windows.sh
# and capture-output.py. Set PZ_NESTED_BUILD to use a configure dir other
# than build/ (daemon.sh honours the same variable via env.sh).
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
case "$OUTPUTS" in
    ''|*[!0-9]*)
        echo "output-count must be a positive integer, got '$OUTPUTS'" >&2
        exit 1
        ;;
esac
if [ "$OUTPUTS" -le 0 ]; then
    echo "output-count must be a positive integer, got '$OUTPUTS'" >&2
    exit 1
fi
WIDTH="${2:-}"
HEIGHT="${3:-}"
SCALE="${4:-}"
for n in "$WIDTH" "$HEIGHT"; do
    case "$n" in
        ''|*[!0-9]*) [ -z "$n" ] || { echo "width/height must be integers" >&2; exit 1; } ;;
    esac
done
if { [ -n "$WIDTH" ] && [ -z "$HEIGHT" ]; } || { [ -z "$WIDTH" ] && [ -n "$HEIGHT" ]; }; then
    echo "width and height must be given together" >&2
    exit 1
fi
case "$SCALE" in
    '') ;;
    *[!0-9.]*|*.*.*|.|*..*|*.)
        echo "scale must be numeric, got '$SCALE'" >&2
        exit 1
        ;;
esac
REPO="$(cd "$(dirname "$0")/../.." && pwd)"
NEST="${PZ_NESTED_DIR:-${XDG_RUNTIME_DIR:-/tmp/pz-nested-$USER}/pz-nested}"
BUILD="${PZ_NESTED_BUILD:-build}"
HOME_N="$NEST/home"

# Refuse to wipe a LIVE session's state out from under it. Existence of the
# socket inode is not liveness (a crashed compositor leaves it behind), so
# probe for a process holding it and quietly unlink a stale one — otherwise
# a crash would lock the harness out until a manual rm. Without fuser the
# probe cannot answer, so the guard fails CLOSED (refuses) rather than
# unlinking what might be a live session's socket. PZ_NESTED_FORCE=1 skips
# the check either way and unlinks; note that a compositor still holding
# the old socket keeps running orphaned and must be killed by hand.
SOCK="${XDG_RUNTIME_DIR:-/run/user/$(id -u)}/pznested"
if [ -e "$SOCK" ] && [ ! -S "$SOCK" ]; then
    echo "refusing: $SOCK exists and is not a socket; remove it by hand" >&2
    exit 1
fi
if [ -S "$SOCK" ] && [ -z "${PZ_NESTED_FORCE:-}" ]; then
    if ! command -v fuser >/dev/null 2>&1; then
        echo "$SOCK exists and fuser is unavailable to tell live from stale;" >&2
        echo "install psmisc, remove the socket by hand, or re-run with PZ_NESTED_FORCE=1" >&2
        exit 1
    fi
    if fuser -s "$SOCK" 2>/dev/null; then
        echo "a nested session is live (a process holds $SOCK);" >&2
        echo "kill it first, or re-run with PZ_NESTED_FORCE=1" >&2
        exit 1
    fi
fi
rm -f "$SOCK"

rm -rf "$HOME_N"
# Stale control files must not survive into the new run: a daemon.sh run
# against a previous session's env.sh would target a dead bus. The kill is
# identity-gated so a recycled pid cannot take down an unrelated process.
if [ -f "$NEST/daemon.pid" ]; then
    OLDPID=$(cat "$NEST/daemon.pid")
    if [ "$(cat "/proc/$OLDPID/comm" 2>/dev/null)" = "plasmazonesd" ]; then
        kill "$OLDPID" 2>/dev/null || true
    fi
fi
rm -f "$NEST/env.sh" "$NEST/daemon.pid" "$NEST/daemon.log"
mkdir -p "$HOME_N/config" "$HOME_N/data" "$HOME_N/cache" "$HOME_N/state"
# env.sh carries the session bus address; keep the tree private even when
# PZ_NESTED_DIR points somewhere world-traversable.
chmod 700 "$NEST"

cat > "$HOME_N/config/kwinrc" <<KWINRC
[Plugins]
kwin_effect_plasmazonesEnabled=true
KWINRC

export XDG_CONFIG_HOME="$HOME_N/config"
export XDG_DATA_HOME="$HOME_N/data"
export XDG_CACHE_HOME="$HOME_N/cache"
export XDG_STATE_HOME="$HOME_N/state"
export QT_QPA_PLATFORM=wayland
# build/bin carries the KWin effect (kwin/effects/plugins/...);
# build/plugins carries the layer-shell QPA integration
# (wayland-shell-integration/phosphorwayland-qpa.so). Without the second
# entry the nested daemon silently loads the INSTALLED layer-shell plugin,
# so layer-shell changes would not actually be under test.
export QT_PLUGIN_PATH="$REPO/$BUILD/bin:$REPO/$BUILD/plugins:${QT_PLUGIN_PATH:-}"
export KWIN_SCREENSHOT_NO_PERMISSION_CHECKS=1
# plasmazones.* covers daemon/effect categories; kwin.effect.plasmazones.*
# covers the effect's screenchange/snapassist categories, which use the
# kwin-prefixed convention and would otherwise stay at warning level.
export QT_LOGGING_RULES="plasmazones.*=true;kwin.effect.plasmazones.*=true"

EXTRA_FLAGS=""
[ -n "$WIDTH" ] && EXTRA_FLAGS="$EXTRA_FLAGS --width $WIDTH"
[ -n "$HEIGHT" ] && EXTRA_FLAGS="$EXTRA_FLAGS --height $HEIGHT"
[ -n "$SCALE" ] && EXTRA_FLAGS="$EXTRA_FLAGS --scale $SCALE"
# Opt-in Xwayland: X11 clients (Proton games are the ones that matter) take
# different KWin paths for fullscreen geometry, so X11-specific bugs cannot
# reproduce in a Wayland-only nested session. Launch X11 test clients with
# QT_QPA_PLATFORM=xcb after sourcing env.sh.
[ -n "${PZ_NESTED_XWAYLAND:-}" ] && EXTRA_FLAGS="$EXTRA_FLAGS --xwayland"

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
    echo \"export QT_LOGGING_RULES='$QT_LOGGING_RULES'\"
    echo \"export PZ_NESTED_DIR='$NEST'\"
    echo \"export PZ_NESTED_REPO='$REPO'\"
    echo \"export PZ_NESTED_BUILD='$BUILD'\"
  } > '$NEST/env.sh.tmp'
  mv '$NEST/env.sh.tmp' '$NEST/env.sh'
  exec kwin_wayland --virtual --output-count $OUTPUTS$EXTRA_FLAGS --socket pznested --no-lockscreen --no-global-shortcuts --no-kactivities
"
