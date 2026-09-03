#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: GPL-3.0-or-later
#
# One-command phosphor-shell harness: a nested kwin_wayland session with
# the BUILD-TREE shell running inside it, drivable over the shell's own
# IPC socket and screenshot-able for eyes-free (AI) test loops. No
# install, no logout, and nothing touches the live session — the power
# menu's logind actions act on the NESTED session's scope, so clicking
# Suspend in here does not suspend the machine you are sitting at.
#
# Usage:
#   scripts/nested-shell/run-shell.sh [output-count] [width height] [scale]
#
# Headless by default (kwin --virtual; drive it with ctl.sh and read it
# with capture.sh). Set PZ_NESTED_VISIBLE=1 to get the nested compositor
# as a window on the host desktop instead, same session semantics, with
# pointer and keyboard.
#
# What it does, in order:
#   1. launches scripts/nested-kwin/run-nested.sh in the background
#      (compositor log: $PZ_NESTED_DIR/compositor.log) and waits for the
#      session's env.sh;
#   2. seeds the example shell tree into the nested config home
#      ($NEST/home/config/phosphor-shell/), so shell.qml edits there
#      hot-reload without rebuilds — seeding must happen AFTER the
#      launcher, whose home wipe would otherwise eat the seed;
#   3. starts the build-tree phosphor-shell inside the session via
#      shell.sh (shell log: $NEST/shell.log, IPC socket:
#      $NEST/phosphor.sock).
#
# Follow-ups (each sources env.sh itself):
#   scripts/nested-shell/ctl.sh call power.toggle     # drive the shell
#   scripts/nested-shell/capture.sh Virtual-1 out.png # look at it
#   scripts/nested-shell/shell.sh                     # restart after a rebuild
#
# Screenshot note, and why the nested-kwin caveat does NOT apply here:
# ScreenShot2 bypasses the PlasmaZones EFFECT chain, which is why the
# sibling harness treats captures as geometry-only evidence. The shell is
# an ordinary layer-shell CLIENT, composited on the normal path, so its
# bars, popouts and toasts DO appear in captures — screenshots are real
# rendering evidence for this harness.
#
# Environment knobs (all inherited by the sibling scripts through env.sh):
#   PZ_NESTED_DIR / PZ_NESTED_SOCKET  — per-worktree isolation, as for
#                                       nested-kwin (defaults pz-nested/
#                                       pznested).
#   PZ_NESTED_BUILD                   — configure dir other than build/.
#   PZ_NESTED_VISIBLE                 — windowed nested compositor.
#   PZ_SHELL_NO_SEED                  — keep the qrc-baked example instead
#                                       of seeding an editable copy.
set -eu

REPO="$(cd "$(dirname "$0")/../.." && pwd)"
RUNTIME_DIR="${XDG_RUNTIME_DIR:-/tmp/pz-nested-$(id -u)}"
NEST="${PZ_NESTED_DIR:-$RUNTIME_DIR/pz-nested}"
BUILD="${PZ_NESTED_BUILD:-build}"

if [ ! -x "$REPO/$BUILD/bin/phosphor-shell" ]; then
    echo "no $BUILD/bin/phosphor-shell — configure with -DBUILD_PHOSPHOR_SHELL=ON and build first" >&2
    exit 1
fi

# A stale env.sh from a crashed previous session must not satisfy the wait
# below — the launcher deletes and rewrites it, and breaking on the stale
# one races the launcher's home wipe (which would eat the seed) and can key
# the socket wait on a dead inode. Record the pre-launch file's identity
# (inode + mtime; the launcher always mv's a fresh tmp file into place, so
# the fresh one never matches) and wait for a DIFFERENT env.sh.
mkdir -p "$NEST"
PRE_ENV_ID=$(stat -c '%i %Y' "$NEST/env.sh" 2>/dev/null || echo none)

# The launcher execs the compositor in the foreground; background it with
# its output on a per-NEST temp name, renamed to the documented
# compositor.log once the session is confirmed up. Per-NEST (not a shared
# name in $RUNTIME_DIR) so concurrent per-worktree sessions cannot clobber
# each other's launch output; a temp name (not compositor.log itself) so
# this invocation cannot truncate a LIVE session's log before the
# launcher's guard refuses; and a same-directory rename so the
# compositor's open log fd stays on the very inode the final path names.
# The launcher's own guards handle a stale or live session in this NEST.
"$REPO/scripts/nested-kwin/run-nested.sh" "$@" > "$NEST/compositor.log.launch" 2>&1 &
LAUNCHER_PID=$!

# env.sh is written just before kwin execs, so a FRESH env.sh is the
# up-signal. The launcher exiting first means its guards refused — surface
# its log instead of spinning the full timeout.
env_fresh() {
    [ "$(stat -c '%i %Y' "$NEST/env.sh" 2>/dev/null || echo none)" != "$PRE_ENV_ID" ] \
        && [ -f "$NEST/env.sh" ]
}
for _ in $(seq 1 100); do
    env_fresh && break
    if ! kill -0 "$LAUNCHER_PID" 2>/dev/null; then
        echo "nested launcher exited before the session came up; its output:" >&2
        cat "$NEST/compositor.log.launch" >&2
        exit 1
    fi
    sleep 0.1
done
if ! env_fresh; then
    echo "timed out waiting for a fresh $NEST/env.sh; launcher output:" >&2
    cat "$NEST/compositor.log.launch" >&2
    exit 1
fi
# Session confirmed up: give the log its documented name. Same-directory
# rename, so the compositor's open fd and the path stay one inode.
mv "$NEST/compositor.log.launch" "$NEST/compositor.log"

# Give the compositor a moment to bind its wayland socket — env.sh lands
# a hair before the exec.
SOCK_PATH="$RUNTIME_DIR/$(. "$NEST/env.sh" && printf %s "$WAYLAND_DISPLAY")"
for _ in $(seq 1 50); do
    [ -S "$SOCK_PATH" ] && break
    sleep 0.1
done
if [ ! -S "$SOCK_PATH" ]; then
    echo "compositor did not bind $SOCK_PATH; see $NEST/compositor.log" >&2
    exit 1
fi

# Seed an EDITABLE copy of the example shell. Without this the binary
# falls back to its qrc-baked copy, which works but cannot be edited, so
# the hot-reload loop — the point of the harness — has nothing to watch.
if [ -z "${PZ_SHELL_NO_SEED:-}" ]; then
    SHELL_CFG="$NEST/home/config/phosphor-shell"
    mkdir -p "$SHELL_CFG"
    cp -r "$REPO/examples/phosphor-shell/." "$SHELL_CFG/"
    echo "seeded editable shell: $SHELL_CFG/shell.qml"
fi

exec "$REPO/scripts/nested-shell/shell.sh"
