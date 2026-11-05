#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: GPL-3.0-or-later
#
# f573-no-dbus-errors-daemon-gone: with the daemon gone (as when its unit is masked), a drag across
# both monitors logs no D-Bus call failures from the effect. Runner: 2 outputs, flag F.
# Staging: p1 opens at 300,300 on Virtual-0. The nested daemon is stopped with its D-Bus
#   activation blocked, so every call the effect makes would error. p1 is then Meta-dragged onto
#   Virtual-1 and back for about 2 s and dropped.
# Asserts: no "D-Bus call failed" line in the compositor log since the daemon stopped.
# A FAIL means: one failure line per setFrameGeometry flush, per crossing and for the endDrag.
set -u
NEST="$1"
LOG="$2"
. "$(dirname "$0")/../lib.sh"
kwjs_ok
need_outputs 2
open p1 300 300
moveto p1 300 300
sleep 1
BEFORE=$(rect "$(geo p1)")
dkill
sleep 3
echo "daemon stopped"
emark
fake abs 450 450 wait 100 keydown 125 wait 50 down left wait 150 \
    path 450 450 1500 450 40 25 wait 200 path 1500 450 400 300 40 25 wait 200 up left wait 80 keyup 125
sleep 2
AFTER=$(rect "$(geo p1)")
echo "p1 before the drag: $BEFORE | after: $AFTER"
[ -n "$AFTER" ] && [ "$AFTER" != "$BEFORE" ] || stage_fail "the drag did not move p1"
elog "D-Bus call failed.*" | sed 's/:.*//' | sort | uniq -c | sed 's/^/    /'
check "D-Bus call failed lines during the drag" "$(elog 'D-Bus call failed' | wc -l)" -eq 0
finish
