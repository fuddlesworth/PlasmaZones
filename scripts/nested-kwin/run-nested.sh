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
# Set PZ_NESTED_PER_OUTPUT_DESKTOPS to any value to seed KWin's per-output
# virtual desktops into the nested kwinrc (the dynamic-workspaces feature and
# the workspace overview need it; a seeded plasmazones config.json then turns
# the feature on without the daemon's consent write).
#
# Set PZ_NESTED_VISIBLE to any value to run the nested compositor as a
# WINDOW on the host desktop instead of headless --virtual: same isolated
# session, but you can watch it and interact with pointer/keyboard.
# Requires a live host WAYLAND_DISPLAY. Defaults to 1600x900 when no
# width/height are given (the headless default of the host's mode size
# makes an unwieldy window). Everything else — bus, homes, env.sh,
# screenshots — behaves identically in both modes.
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
        '') ;;
        *[!0-9]*) echo "width/height must be positive integers, got '$n'" >&2; exit 1 ;;
        *)
            # Zero is a well-formed integer and a useless output dimension:
            # kwin takes it and comes up with a degenerate screen, which reads
            # as the harness being broken. Rejected for the same reason
            # output-count rejects it above.
            [ "$n" -gt 0 ] || { echo "width/height must be positive integers, got '$n'" >&2; exit 1; }
            ;;
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
    # Well-formed but degenerate: 0, 0.0, .0 and friends all parse as numeric
    # and scale every output to nothing.
    0|0.|0.0|0.00|.0|.00)
        echo "scale must be greater than zero, got '$SCALE'" >&2
        exit 1
        ;;
esac
REPO="$(cd "$(dirname "$0")/../.." && pwd)"
# ONE fallback for a missing XDG_RUNTIME_DIR, resolved once. The state dir and
# the wayland socket both live under it, and giving them different fallbacks
# (/tmp for one, /run/user/UID for the other) splits the session in half on
# exactly the systems where the variable is absent: the socket lands somewhere
# unwritable while env.sh reports success from /tmp.
RUNTIME_DIR="${XDG_RUNTIME_DIR:-/tmp/pz-nested-$(id -u)}"
mkdir -p "$RUNTIME_DIR"
NEST="${PZ_NESTED_DIR:-$RUNTIME_DIR/pz-nested}"
BUILD="${PZ_NESTED_BUILD:-build}"
HOME_N="$NEST/home"

# Refuse to wipe a LIVE session's state out from under it. Existence of the
# socket inode is not liveness (a crashed compositor leaves it behind), so
# probe /proc/net/unix for a bound listener on the path and quietly unlink
# a stale one — otherwise a crash would lock the harness out until a manual
# rm. PZ_NESTED_FORCE=1 skips the check and unlinks; note that a compositor
# still holding the old socket keeps running orphaned and must be killed by
# hand.
# Socket name, so two worktrees can each run a nested session at once (kwin
# takes a lockfile named after it, and a second session on the same name dies
# with "could not add wayland socket"). Pair a distinct PZ_NESTED_SOCKET with
# a distinct PZ_NESTED_DIR — the state dir holds the env.sh every follow-up
# script sources, so sharing one across two sessions points them both at
# whichever started last.
PZ_NESTED_SOCKET="${PZ_NESTED_SOCKET:-pznested}"
# Validated like the numeric arguments above, and for a sharper reason: this
# one is interpolated into the `sh -c` command text and into env.sh below, so a
# name carrying a space, a quote or a shell metacharacter either breaks every
# follow-up script or runs as shell. It is a wayland socket NAME, so the
# character class is not a restriction anyone will notice.
case "$PZ_NESTED_SOCKET" in
    ''|*[!A-Za-z0-9._-]*)
        echo "PZ_NESTED_SOCKET must be a plain socket name matching [A-Za-z0-9._-]+, got '$PZ_NESTED_SOCKET'" >&2
        exit 1
        ;;
esac
SOCK="$RUNTIME_DIR/$PZ_NESTED_SOCKET"
if [ -e "$SOCK" ] && [ ! -S "$SOCK" ]; then
    echo "refusing: $SOCK exists and is not a socket; remove it by hand" >&2
    exit 1
fi
if [ -S "$SOCK" ] && [ -z "${PZ_NESTED_FORCE:-}" ]; then
    # Liveness probe via /proc/net/unix, which lists every bound unix
    # socket by the path given at bind time. fuser is the wrong tool here
    # and was observed false-negating on a live compositor: a bound
    # listening socket shows up in the holder's fd table as
    # socket:[inode], not as the filesystem path, so a path-keyed fuser
    # finds nothing and the guard waves a second run through to wipe the
    # live session's state.
    if awk -v p="$SOCK" '$NF == p { found = 1 } END { exit !found }' /proc/net/unix; then
        echo "a nested session is live (a process has $SOCK bound);" >&2
        echo "kill it first, or re-run with PZ_NESTED_FORCE=1" >&2
        exit 1
    fi
fi
rm -f "$SOCK"

# The socket guard above keys on PZ_NESTED_SOCKET while everything below
# destroys PZ_NESTED_DIR, so two runs with distinct sockets and a shared or
# defaulted dir sail past it and then delete the first session's state out
# from under its running compositor. Probe the dir the same way: if the
# previous env.sh names a bus whose socket is still there, a session is live
# in THIS tree.
if [ -f "$NEST/env.sh" ] && [ -z "${PZ_NESTED_FORCE:-}" ]; then
    # The exclusion class MUST stop at ',' too: dbus-run-session addresses
    # look like unix:path=/tmp/dbus-XXX,guid=..., and letting the capture
    # run through the comma yields a path with ',guid=...' glued on, whose
    # -S test always fails — the guard then never fires and a second run
    # wipes a LIVE session's state (observed exactly that way).
    OLDBUS=$(sed -n 's/^export DBUS_SESSION_BUS_ADDRESS=.*unix:path=\([^;,'"'"'"]*\).*/\1/p' "$NEST/env.sh" 2>/dev/null | head -n1)
    if [ -n "$OLDBUS" ] && [ -S "$OLDBUS" ]; then
        echo "a nested session is live in $NEST (its bus socket $OLDBUS still exists);" >&2
        echo "point PZ_NESTED_DIR somewhere else, kill that session, or re-run with PZ_NESTED_FORCE=1" >&2
        exit 1
    fi
fi

# PZ_NESTED_DIR is interpolated straight into the rm -rf and the chmod below.
# An operator typo of / or $HOME would take out /home or open the home
# directory up, so require something that looks like a scratch dir: absolute,
# and at least two components deep. Unset is safe already — it falls through
# to the default above.
#
# The character class matters as much as the depth. This path is also written
# unquoted into the generated D-Bus service file's Exec= line, which is parsed
# as a command line and cannot express a path containing whitespace, and it is
# interpolated into two heredocs where a quote or a dollar sign would either
# break the shim or run as shell. A scratch directory has no business carrying
# any of that.
case "$NEST" in
    /*/*) ;;
    *)
        echo "refusing: PZ_NESTED_DIR must be an absolute path at least two components deep, got '$NEST'" >&2
        exit 1
        ;;
esac
case "$NEST" in
    *[!A-Za-z0-9._/-]*)
        echo "refusing: PZ_NESTED_DIR must match [A-Za-z0-9._/-]+ (no whitespace or shell metacharacters), got '$NEST'" >&2
        exit 1
        ;;
esac

rm -rf "$HOME_N"
# Stale control files must not survive into the new run: a daemon.sh run
# against a previous session's env.sh would target a dead bus.
#
# The kill is identity-gated on the bus address, not on comm alone: comm only
# proves the pid is A plasmazonesd, and after a pid recycle that can be the
# user's real host-session daemon. Every nested daemon runs on a
# dbus-run-session bus under /tmp/dbus-, which the login bus never does.
if [ -f "$NEST/daemon.pid" ]; then
    OLDPID=$(cat "$NEST/daemon.pid")
    if [ "$(cat "/proc/$OLDPID/comm" 2>/dev/null)" = "plasmazonesd" ] \
        && tr '\0' '\n' < "/proc/$OLDPID/environ" 2>/dev/null \
        | grep -q '^DBUS_SESSION_BUS_ADDRESS=unix:path=/tmp/dbus-'; then
        kill "$OLDPID" 2>/dev/null || true
        # Wait for it to actually go before dropping its only pid record,
        # otherwise a slow exit leaves an untracked daemon behind.
        for _ in 1 2 3 4 5 6 7 8 9 10; do
            [ -d "/proc/$OLDPID" ] || break
            sleep 0.2
        done
        [ -d "/proc/$OLDPID" ] && kill -9 "$OLDPID" 2>/dev/null || true
    fi
fi
rm -f "$NEST/env.sh" "$NEST/daemon.pid" "$NEST/daemon.log"
mkdir -p "$HOME_N/config" "$HOME_N/data" "$HOME_N/cache" "$HOME_N/state"
# env.sh carries the session bus address; keep the tree private even when
# PZ_NESTED_DIR points somewhere world-traversable.
chmod 700 "$NEST"

# Both build-tree effect plugins: the main effect and the workspace overview
# (kwin_effect_plasmazones_overview, a QuickSceneEffect that renders the
# dynamic-workspaces map). The overview only draws once the daemon's
# workspaces feature is on, which needs KWin's per-output virtual desktops;
# PZ_NESTED_PER_OUTPUT_DESKTOPS opts the nested kwinrc into that mode so a
# seeded config can enable the feature without the consent write.
cat > "$HOME_N/config/kwinrc" <<KWINRC
[Plugins]
kwin_effect_plasmazonesEnabled=true
kwin_effect_plasmazones_overviewEnabled=true
KWINRC
if [ -n "${PZ_NESTED_PER_OUTPUT_DESKTOPS:-}" ]; then
    cat >> "$HOME_N/config/kwinrc" <<KWINRC

[Windows]
PerOutputVirtualDesktops=true
KWINRC
fi

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
# Refuse a missing build tree rather than starting a session that silently
# tests the INSTALLED effect. With $REPO/$BUILD absent QT_PLUGIN_PATH points
# at nothing, kwin_wayland loads whatever is installed, and the run looks
# entirely normal while exercising code that is not under test — the exact
# failure the header warns about.
if [ ! -d "$REPO/$BUILD/bin" ]; then
    echo "no build tree at $REPO/$BUILD/bin; configure and build first," >&2
    echo "or set PZ_NESTED_BUILD to the right configure dir" >&2
    exit 1
fi
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

# Backend selection: headless virtual outputs by default; a visible nested
# window when PZ_NESTED_VISIBLE is set. The visible backend needs the host
# compositor, so fail early with the reason instead of letting kwin die
# with a bare backend error.
BACKEND_FLAGS="--virtual --output-count $OUTPUTS"
if [ -n "${PZ_NESTED_VISIBLE:-}" ]; then
    if [ -z "${WAYLAND_DISPLAY:-}" ]; then
        echo "PZ_NESTED_VISIBLE needs a live host Wayland session (WAYLAND_DISPLAY is unset)" >&2
        exit 1
    fi
    BACKEND_FLAGS="--output-count $OUTPUTS"
    # A visible window with no requested size inherits the host output's
    # full mode, which is unwieldy; pick a sane default when the caller
    # gave none.
    if [ -z "$WIDTH" ]; then
        EXTRA_FLAGS="$EXTRA_FLAGS --width 1600 --height 900"
    fi
fi

exec dbus-run-session -- sh -c "
  # set -e inside the child: the outer set -eu does NOT cross an sh -c, so
  # without this a failed env.sh write (full disk, read-only or unwritable
  # runtime dir) falls straight through to the exec below and the compositor
  # comes up healthy with no state file — after which every helper script
  # reports 'no nested session' while a session is in fact running.
  set -e
  {
    echo \"export DBUS_SESSION_BUS_ADDRESS='\$DBUS_SESSION_BUS_ADDRESS'\"
    echo \"export WAYLAND_DISPLAY='$PZ_NESTED_SOCKET'\"
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
  # \$EXTRA_FLAGS stays unquoted on purpose: it is a flag LIST and needs word
  # splitting. Everything else is quoted.
  # \$BACKEND_FLAGS is a flag list like \$EXTRA_FLAGS and needs the same
  # word splitting.
  exec kwin_wayland $BACKEND_FLAGS$EXTRA_FLAGS --socket '$PZ_NESTED_SOCKET' --no-lockscreen --no-global-shortcuts --no-kactivities
"
