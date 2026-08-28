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
# Set PZ_NESTED_XWAYLAND to any value to start Xwayland in the nested
# session (X11 test clients; see the flag site below for how to point a
# client at the nested display).
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
# PZ_NESTED_SOCKET (default pznested) names the wayland socket. Two nested
# sessions cannot share one — kwin locks on the name and the second dies with
# "could not add wayland socket" — so to run one per worktree give each its
# own PZ_NESTED_SOCKET and its own PZ_NESTED_DIR:
#   PZ_NESTED_SOCKET=pzfoo PZ_NESTED_DIR="$XDG_RUNTIME_DIR/pz-nested-foo" \
#     scripts/nested-kwin/run-nested.sh
# The sibling scripts take PZ_NESTED_DIR the same way.
#
# Gotchas learned the hard way:
#   - fish cannot source env.sh (POSIX `export VAR=...`); use bash -c.
#   - The effect log needs QT_FORCE_STDERR_LOGGING=1 (set below and carried
#     in env.sh). Without it Qt routes messages to journald whenever stderr
#     is not a tty, so the nested log is empty AND `journalctl --user` shows
#     the HOST daemon and effect, which are indistinguishable at a glance
#     from the nested ones. Check the compositor's own output, not the
#     journal.
#   - Do not `pkill -f` a pattern that appears in your own command line.
#   - A stock session bus D-Bus-activates the INSTALLED daemon the moment
#     the effect connects, which then answers with the HOST's screens. This
#     script shadows the service file so activation starts the build-tree
#     daemon in-session instead; daemon.sh still evicts whoever holds the
#     name, so the run has one daemon with a known pid and log.
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
# Socket name, so two worktrees can each run a nested session at once (kwin
# takes a lockfile named after it, and a second session on the same name dies
# with "could not add wayland socket"). Pair a distinct PZ_NESTED_SOCKET with
# a distinct PZ_NESTED_DIR — the state dir holds the env.sh every follow-up
# script sources, so sharing one across two sessions points them both at
# whichever started last.
PZ_NESTED_SOCKET="${PZ_NESTED_SOCKET:-pznested}"
SOCK="${XDG_RUNTIME_DIR:-/run/user/$(id -u)}/$PZ_NESTED_SOCKET"
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
# Without this the rules above buy you nothing. Qt's default handler sends
# messages to JOURNALD whenever stderr is not a tty, which is exactly the case
# here (the compositor's output is redirected), so the nested effect's log
# vanishes from this terminal. Worse than vanishing: `journalctl --user` then
# shows the HOST session's daemon and effect instead, which look like the
# nested ones and are not, so a run can be read completely backwards.
export QT_FORCE_STDERR_LOGGING=1

# D-BUS ACTIVATION, redirected to the build tree.
#
# The system ships /usr/share/dbus-1/services/org.plasmazones.service, so the
# nested bus activates the INSTALLED daemon the moment the effect first calls
# org.plasmazones. That daemon inherits the bus's environment — which names the
# HOST compositor, not this session — so it connects to the wrong Wayland
# display, answers queries with the host's screens, and outlives the nested bus
# when it dies. They accumulate across runs, compete for the bus name, and
# eventually make daemon.sh lose the race, at which point a run silently tests
# the host session. That failure reads exactly like a behaviour change.
#
# Shadowing the service file ahead of the system one turns activation from a
# hazard into the correct behaviour: the name resolves to the build-tree daemon
# started inside this session. dbus-daemon takes the FIRST match while scanning
# XDG_DATA_DIRS in order, so prepending the shadow leaves every other service
# (kglobalaccel, the portals) resolving from /usr/share as before.
SVC_DIR="$NEST/dbus-services/dbus-1/services"
mkdir -p "$SVC_DIR"
# The service file cannot carry environment, so it execs a wrapper that sources
# the session's own env.sh first. env.sh is written just before kwin_wayland
# execs, and activation can only happen after a client connects, so it is
# always present by the time this runs.
cat > "$NEST/activate-daemon.sh" <<ACTIVATE
#!/bin/sh
# D-Bus activation shim for the nested session. Never invoked by hand.
set -eu
. "$NEST/env.sh"
exec "$REPO/$BUILD/bin/plasmazonesd"
ACTIVATE
chmod +x "$NEST/activate-daemon.sh"
cat > "$SVC_DIR/org.plasmazones.service" <<SVC
[D-BUS Service]
Name=org.plasmazones
Exec=$NEST/activate-daemon.sh
SVC
export XDG_DATA_DIRS="$NEST/dbus-services:${XDG_DATA_DIRS:-/usr/local/share:/usr/share}"

EXTRA_FLAGS=""
[ -n "$WIDTH" ] && EXTRA_FLAGS="$EXTRA_FLAGS --width $WIDTH"
[ -n "$HEIGHT" ] && EXTRA_FLAGS="$EXTRA_FLAGS --height $HEIGHT"
[ -n "$SCALE" ] && EXTRA_FLAGS="$EXTRA_FLAGS --scale $SCALE"
# Opt-in Xwayland: X11 clients (Proton games are the ones that matter) take
# different KWin paths for fullscreen geometry, so X11-specific bugs cannot
# reproduce in a Wayland-only nested session. env.sh cannot carry the X11
# DISPLAY — it is written before kwin_wayland execs, and the nested
# Xwayland picks its display number at startup (kwin_wayland usually
# prints it on this terminal; if not, `ls /tmp/.X11-unix` and pick the
# socket that appeared). To run an X11 test client: source env.sh, find
# the display number, then
#   DISPLAY=:<n> QT_QPA_PLATFORM=xcb <client>
# (QT_QPA_PLATFORM=xcb alone would connect to the HOST X server and
# silently test the wrong compositor).
[ -n "${PZ_NESTED_XWAYLAND:-}" ] && EXTRA_FLAGS="$EXTRA_FLAGS --xwayland"

exec dbus-run-session -- sh -c "
  {
    echo \"export DBUS_SESSION_BUS_ADDRESS='\$DBUS_SESSION_BUS_ADDRESS'\"
    echo \"export WAYLAND_DISPLAY=$PZ_NESTED_SOCKET\"
    echo \"export XDG_CONFIG_HOME='$XDG_CONFIG_HOME'\"
    echo \"export XDG_DATA_HOME='$XDG_DATA_HOME'\"
    echo \"export XDG_CACHE_HOME='$XDG_CACHE_HOME'\"
    echo \"export XDG_STATE_HOME='$XDG_STATE_HOME'\"
    echo \"export XDG_DATA_DIRS='$XDG_DATA_DIRS'\"
    echo \"export QT_QPA_PLATFORM=wayland\"
    echo \"export QT_PLUGIN_PATH='$QT_PLUGIN_PATH'\"
    echo \"export KWIN_SCREENSHOT_NO_PERMISSION_CHECKS=1\"
    echo \"export QT_LOGGING_RULES='$QT_LOGGING_RULES'\"
    echo \"export QT_FORCE_STDERR_LOGGING=1\"
    echo \"export PZ_NESTED_DIR='$NEST'\"
    echo \"export PZ_NESTED_REPO='$REPO'\"
    echo \"export PZ_NESTED_BUILD='$BUILD'\"
  } > '$NEST/env.sh.tmp'
  mv '$NEST/env.sh.tmp' '$NEST/env.sh'
  exec kwin_wayland --virtual --output-count $OUTPUTS$EXTRA_FLAGS --socket $PZ_NESTED_SOCKET --no-lockscreen --no-global-shortcuts --no-kactivities
"
