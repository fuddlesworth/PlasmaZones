#!/bin/bash
# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Start the BUILD-TREE plasmazonesd inside the nested session. Kills the
# installed daemon if D-Bus activation already claimed org.plasmazones on
# the nested bus (it connects to the HOST compositor and answers with the
# host's screens, poisoning every query). Never touches the host session's
# own daemon.
set -eu
NEST="${PZ_NESTED_DIR:-/tmp/pz-nested-$USER}"
. "$NEST/env.sh"
BUSPID=$(dbus-send --session --print-reply --dest=org.freedesktop.DBus / \
    org.freedesktop.DBus.GetConnectionUnixProcessID string:org.plasmazones 2>/dev/null \
    | awk '/uint32/{print $2}') || true
if [ -n "${BUSPID:-}" ]; then
    kill "$BUSPID" 2>/dev/null || true
    sleep 1
fi
nohup "$PZ_NESTED_REPO/build/bin/plasmazonesd" > "$NEST/daemon.log" 2>&1 &
echo $! > "$NEST/daemon.pid"
sleep 3
if ps -p "$(cat "$NEST/daemon.pid")" >/dev/null; then
    echo "daemon up (pid $(cat "$NEST/daemon.pid")); screens:"
    qdbus6 org.plasmazones /PlasmaZones org.plasmazones.Screen.getPhysicalScreens
else
    echo "daemon FAILED; see $NEST/daemon.log" >&2
    exit 1
fi
