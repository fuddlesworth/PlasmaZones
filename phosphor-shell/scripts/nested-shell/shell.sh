#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: GPL-3.0-or-later
#
# (Re)start the build-tree phosphor-shell inside the nested session.
# QML edits in the seeded config hot-reload on their own; this exists for
# the C++ side — rebuild, run shell.sh, and the new binary is live in
# seconds. Evicts a previous harness shell by recorded pid first, so
# there is exactly one shell with a known pid and log per session.
#
#   log:    $PZ_NESTED_DIR/shell.log   (QT_FORCE_STDERR_LOGGING is already
#           in env.sh, so category output lands here, not in journald)
#   socket: $PZ_NESTED_DIR/phosphor.sock  (via $PHOSPHOR_SOCKET — a
#           per-session path, so a shell on the HOST session keeps its own
#           $XDG_RUNTIME_DIR/phosphor.sock undisturbed)
#   pid:    $PZ_NESTED_DIR/shell.pid
set -eu

REPO="$(cd "$(dirname "$0")/../.." && pwd)"
RUNTIME_DIR="${XDG_RUNTIME_DIR:-/tmp/pz-nested-$(id -u)}"
NEST="${PZ_NESTED_DIR:-$RUNTIME_DIR/pz-nested}"

if [ ! -f "$NEST/env.sh" ]; then
    echo "no nested session in $NEST (run scripts/nested-shell/run-shell.sh first)" >&2
    exit 1
fi
# shellcheck disable=SC1091
. "$NEST/env.sh"
BUILD="${PZ_NESTED_BUILD:-build}"

# Evict by RECORDED pid, never by name: a pkill pattern would also hit a
# shell running against the host session in another terminal.
if [ -f "$NEST/shell.pid" ]; then
    OLD=$(cat "$NEST/shell.pid")
    case "$OLD" in
        *[!0-9]*|'') ;;
        *)
            if kill -0 "$OLD" 2>/dev/null; then
                kill "$OLD" 2>/dev/null || true
                # Bounded wait so a wedged shell cannot hang the restart.
                for _ in $(seq 1 30); do
                    kill -0 "$OLD" 2>/dev/null || break
                    sleep 0.1
                done
                kill -9 "$OLD" 2>/dev/null || true
            fi
            ;;
    esac
    rm -f "$NEST/shell.pid"
fi

export PHOSPHOR_SOCKET="$NEST/phosphor.sock"
nohup "$REPO/$BUILD/bin/phosphor-shell" > "$NEST/shell.log" 2>&1 &
echo $! > "$NEST/shell.pid"

# Up means the IPC socket is bound: the shell binds it early in main, so
# it doubles as the liveness probe, and it is the thing every ctl.sh
# caller is about to need anyway.
for _ in $(seq 1 50); do
    [ -S "$PHOSPHOR_SOCKET" ] && break
    if ! kill -0 "$(cat "$NEST/shell.pid")" 2>/dev/null; then
        echo "phosphor-shell exited at startup; its log:" >&2
        cat "$NEST/shell.log" >&2
        exit 1
    fi
    sleep 0.1
done
if [ ! -S "$PHOSPHOR_SOCKET" ]; then
    echo "shell is running but never bound $PHOSPHOR_SOCKET; see $NEST/shell.log" >&2
    exit 1
fi
echo "phosphor-shell up (pid $(cat "$NEST/shell.pid"), log $NEST/shell.log, socket $PHOSPHOR_SOCKET)"
