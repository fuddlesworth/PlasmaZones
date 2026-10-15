#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: GPL-3.0-or-later
#
# f811-autotile-drag-return: a plain drag that leaves an autotile monitor and comes back is seen
# coming back, and the drop there floats the window. Runner: 2 outputs, flag F.
# Staging: Virtual-0 snapping (COLS3), Virtual-1 autotile:bsp with t1 and t2 tiled, zone selector
#   and Snap Assist off, the autotile drag behaviour at its Float default. t1 is Meta-dragged (no
#   activation trigger) onto Virtual-0, back onto Virtual-1, and dropped there.
# Asserts: the effect logs the policy flip back to EngineOwnedScreen on Virtual-1 for t1, the drop
#   is an endDrag ApplyFloat on Virtual-1, t1 ends on Virtual-1 at its pre-autotile size, and the
#   daemon reads it floating.
# A FAIL means: the flip onto Virtual-0 reset the activation latch and the return went unseen.
set -u
NEST="$1"
LOG="$2"
. "$(dirname "$0")/../lib.sh"
kwjs_ok
need_outputs 2
setting zoneSelectorEnabled false
setting snapAssistEnabled false
assign Virtual-0 "$COLS3"
assign Virtual-1 "autotile:bsp"
sleep 2
open t1 300 300
FREE=$(size "$(geo t1)")
sendto t1 Virtual-1 || stage_fail "t1 did not reach Virtual-1 ($(geo t1))"
open t2 300 300
sendto t2 Virtual-1 || stage_fail "t2 did not reach Virtual-1 ($(geo t2))"
sleep 1
ids
T1U=$(uuid t1)
[ -n "$T1U" ] || stage_fail "t1 never mapped"
G=$(geo t1)
echo "start: t1 $G (free size $FREE) | t2 $(geo t2)"
[ "$(size "$G")" != "$FREE" ] || stage_fail "t1 was not tiled on Virtual-1 ($G)"
read -r X Y W H _ <<< "$G"
GX=$((${X%.*} + ${W%.*} / 2))
GY=$((${Y%.*} + ${H%.*} / 2))
emark
fake abs "$GX" "$GY" wait 100 keydown 125 wait 50 down left wait 150 \
    path "$GX" "$GY" 400 384 20 30 wait 900 path 400 384 1400 300 20 30 wait 900 \
    up left wait 80 keyup 125
sleep 2.5
G=$(geo t1)
echo "after: t1 $G | t2 $(geo t2)"
elog "slotDragPolicyChanged: .*" | sed 's/^/    /'
[ -n "$(elog "slotDragPolicyChanged: [^ ]*$T1U[^ ]* DragBypassReason::EngineOwnedScreen -> DragBypassReason::None")" ] \
    || stage_fail "the drag never left the autotile policy over Virtual-0"
check "policy flips back to EngineOwnedScreen on Virtual-1 for t1" \
    "$(elog "slotDragPolicyChanged: [^ ]*$T1U[^ ]* DragBypassReason::None -> DragBypassReason::EngineOwnedScreen screen= \"Virtual-1\"" | wc -l)" -ge 1
check "the drop is an ApplyFloat on Virtual-1" \
    "$(elog "endDrag ApplyFloat: [^ ]*$T1U[^ ]* on \"Virtual-1\"" | wc -l)" -ge 1
check "t1's output after the drop" "$(outof "$G")" = Virtual-1
check "t1's size after the drop (its pre-autotile size)" "$(size "$G")" = "$FREE"
check "the daemon reads t1 floating" "$(floating "org.pz.plain|$T1U")" = true
finish
