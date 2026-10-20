#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: GPL-3.0-or-later
#
# f593-tiling-float-refloat: a window floated again after a drag-insert unfloat is announced on the
# Tiling float channel again, so the effect drops it from the monocle. Runner: 1 output, flag F.
# Staging: one output on autotile:monocle, a1 and a2 tiled, a dbus-monitor on
#   Tiling.windowFloatingChanged. The float shortcut floats a2; a2 is then Meta-dragged with Alt
#   (the activation trigger) back into the stack and dropped (the drag-insert commit, a passive
#   unfloat that reaches only the WindowTracking channel); then the float shortcut floats a2 again.
# Asserts: two Tiling.windowFloatingChanged(a2, true) signals in all, and after the second float
#   a2 is not maximized and is the active window.
# A FAIL means: the second (a2, true) was deduplicated against the first, so the effect kept a2 a
#   monocle member, maximized while floating.
set -u
NEST="$1"
LOG="$2"
. "$(dirname "$0")/../lib.sh"
kwjs_ok
assign Virtual-0 "autotile:monocle"
sleep 2
for c in a1 a2; do open "$c"; done
ids
A2U=$(uuid a2)
[ -n "$A2U" ] || stage_fail "a2 never mapped"
scene() { echo "active=$(activecap) | a1 $(geo a1) | a2 $(geo a2)"; }
float() {
    shortcut toggle_window_float
    sleep 2
}
MON="$NEST/f593-monitor.log"
mon_start "$MON" "type='signal',interface='org.plasmazones.Tiling',member='windowFloatingChanged'"
echo "monocle with a1, a2: $(scene)"
[ "$(maxof "$(geo a2)")" = 3 ] || stage_fail "a2 is not monocle-maximized at the start ($(geo a2))"
activate a2
float
echo "after the first float: Tiling signals for a2=[$(wfc "$MON" "$A2U")] | $(scene)"
[ "$(wfct "$MON" "$A2U")" = 1 ] || stage_fail "the first float did not announce a2 floating once on the Tiling channel"
# Meta-drag a2 from its centre to the middle of the output with Alt held once the move is under
# way (KWin starts a Meta move only with Meta alone).
read -r X Y W H _ <<< "$(geo a2)"
GX=$((${X%.*} + ${W%.*} / 2))
GY=$((${Y%.*} + ${H%.*} / 2))
fake abs "$GX" "$GY" wait 100 keydown 125 wait 50 down left wait 150 \
    keydown 56 wait 150 path "$GX" "$GY" 800 450 20 30 wait 900 up left wait 80 keyup 56 keyup 125
sleep 2.5
echo "after the drag-insert drop: $(scene)"
[ "$(maxof "$(geo a2)")" = 3 ] || stage_fail "the drag-insert drop did not tile a2 again ($(geo a2))"
activate a2
float
echo "after the second float: Tiling signals for a2=[$(wfc "$MON" "$A2U")] | $(scene)"
check "Tiling.windowFloatingChanged(a2, true) signals after the second float" "$(wfct "$MON" "$A2U")" -eq 2
check "a2's maximize mode after the second float" "$(maxof "$(geo a2)")" = 0
check "the active window after the second float" "$(activecap)" = a2
finish
