#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Start the BUILD-TREE plasmazonesd inside the nested session. run-nested.sh
# shadows the D-Bus service file, so activation on the nested bus already
# starts a build-tree daemon in-session; this evicts whatever holds the name
# so the run has one daemon with a known pid and log, and reaps daemons left
# behind by earlier sessions whose bus is gone. Never touches the host
# session's own daemon.
set -eu
NEST="${PZ_NESTED_DIR:-${XDG_RUNTIME_DIR:-/tmp/pz-nested-$USER}/pz-nested}"
if [ ! -r "$NEST/env.sh" ]; then
    echo "no nested session state at $NEST/env.sh; start run-nested.sh first" >&2
    exit 1
fi
. "$NEST/env.sh"

# env.sh is written just before kwin_wayland execs, so on a fast follow-up
# the compositor may not be listening yet. Wait for the socket, bounded.
SOCK="${XDG_RUNTIME_DIR:-/run/user/$(id -u)}/$WAYLAND_DISPLAY"
for _ in $(seq 1 50); do
    [ -S "$SOCK" ] && break
    sleep 0.2
done
if [ ! -S "$SOCK" ]; then
    echo "nested compositor socket $SOCK never appeared; is run-nested.sh up?" >&2
    exit 1
fi

# A leftover daemon from a previous nested run is invisible to the bus
# check below (its bus died with the old session); kill it by pid first,
# identity-gated so a recycled pid cannot hit an unrelated process.
if [ -f "$NEST/daemon.pid" ]; then
    OLDPID=$(cat "$NEST/daemon.pid")
    if [ "$(cat "/proc/$OLDPID/comm" 2>/dev/null)" = "plasmazonesd" ]; then
        kill "$OLDPID" 2>/dev/null || true
    fi
    rm -f "$NEST/daemon.pid"
fi
BUSPID=$(dbus-send --session --print-reply --dest=org.freedesktop.DBus / \
    org.freedesktop.DBus.GetConnectionUnixProcessID string:org.plasmazones 2>/dev/null \
    | awk '/uint32/{print $2}') || true
if [ -n "${BUSPID:-}" ]; then
    kill "$BUSPID" 2>/dev/null || true
    sleep 1
fi

# Reap daemons stranded on a DEAD nested bus. run-nested.sh now shadows the
# D-Bus service file so activation starts the build-tree daemon in-session
# rather than the installed one on the host, but a session started before that
# fix — or killed hard enough to orphan its activation — leaves one behind.
# They are invisible to the bus check above (their own bus is gone), they
# accumulate across runs, and once several are competing the eviction here
# starts losing the race and a run silently answers with the HOST's screens.
#
# Matched on the bus address in the process environment, so only daemons that
# belong to a pz-nested bus can be hit. The user's real session daemon has the
# login bus address and is never matched.
for pid in $(pgrep -x plasmazonesd 2>/dev/null || true); do
    [ "$pid" = "${BUSPID:-}" ] && continue
    peer_bus=$(tr '\0' '\n' < "/proc/$pid/environ" 2>/dev/null \
        | sed -n 's/^DBUS_SESSION_BUS_ADDRESS=//p') || true
    case "$peer_bus" in
        "unix:path=/tmp/dbus-"*)
            # A pz-nested bus socket that no longer exists means the session it
            # belonged to is gone and this daemon is stranded.
            #
            # unix:path= specifically, not any address mentioning /tmp/dbus-:
            # a dbus built for ABSTRACT sockets reads unix:abstract=/tmp/dbus-…,
            # which the strip below cannot turn into a filesystem path, so the
            # -S test would answer false for a perfectly LIVE bus and kill its
            # daemon. An address whose liveness cannot be checked is left alone.
            sock=${peer_bus#unix:path=}
            sock=${sock%%,*}
            [ -S "$sock" ] || kill "$pid" 2>/dev/null || true
            ;;
    esac
done
BUILD="${PZ_NESTED_BUILD:-build}"
nohup "$PZ_NESTED_REPO/$BUILD/bin/plasmazonesd" > "$NEST/daemon.log" 2>&1 &
echo $! > "$NEST/daemon.pid"

# "Up" means it OWNS the bus name, not merely that the pid exists — a
# daemon that started but failed to claim org.plasmazones (or died into a
# zombie) would otherwise print success while every later qdbus6 call
# answers from nothing.
up=""
for _ in $(seq 1 30); do
    OWNER=$(dbus-send --session --print-reply --dest=org.freedesktop.DBus / \
        org.freedesktop.DBus.GetNameOwner string:org.plasmazones 2>/dev/null | awk '/string/{print $2}') || true
    if [ -n "${OWNER:-}" ]; then
        up=1
        break
    fi
    sleep 0.2
done
if [ -n "$up" ]; then
    echo "daemon up (pid $(cat "$NEST/daemon.pid"), owns org.plasmazones); screens:"
    qdbus6 org.plasmazones /PlasmaZones org.plasmazones.Screen.getPhysicalScreens
else
    echo "daemon FAILED to claim org.plasmazones; see $NEST/daemon.log" >&2
    exit 1
fi
