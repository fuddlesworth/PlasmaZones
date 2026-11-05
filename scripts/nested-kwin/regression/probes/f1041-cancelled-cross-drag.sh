#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: GPL-3.0-or-later
#
# f1041-cancelled-cross-drag: an activated drag KWin cancels while it is over another monitor keeps
# the window's zone. Runner: 2 outputs, flag F.
# Staging: Virtual-0 and Virtual-1 COLS3, Snap Assist off, w snapped into Virtual-0's zone 1. The
#   gesture is a KEYBOARD move, because only a move KWin itself cancels sets the daemon's
#   external-cancel flag (a pointer drag holds the effect's keyboard grab, which turns Escape into a
#   cancelSnap). w active, workspace.slotWindowMove(), Alt (the default snap trigger) held so the next
#   cursor tick promotes the pending snap drag, Alt+Right arrows until the pointer is 80 px inside
#   Virtual-1, held 1.2 s, Alt released, 0.6 s, Escape: KWin cancels and puts w back. Staging gates:
#   the effect logs reverted= true, the daemon logs the promotion, and endDrag's cursor x is at or
#   past Virtual-1's left edge.
# Asserts: no "Cross-screen drag: releasing" line for w, w keeps zone 1, and w sits on its zone-1
#   rect (KWin's cancel).
# A FAIL means: the cross-screen block ran for the cancelled drag and w lost its zone while KWin put
#   it back.
set -u
NEST="$1"
LOG="$2"
. "$(dirname "$0")/../lib.sh"
kwjs_ok
need_outputs 2
read -r BX _ _ _ <<< "$(scr Virtual-1)"
setting snapAssistEnabled false
assign Virtual-0 "$COLS3"
assign Virtual-1 "$COLS3"
sleep 2
open w
sendto w Virtual-0 || stage_fail "w not on Virtual-0"
ids
WU=$(uuid w)
[ -n "$WU" ] || stage_fail "w never mapped"
W="org.pz.plain|$WU"
PZ org.plasmazones.Control.snapWindowToZone "$W" 1 Virtual-0 >/dev/null
sleep 2
Z=$(zoneof "$W")
G=$(geo w)
echo "staged: w $G zone=$Z"
case "$Z" in "{"*) ;; *) stage_fail "w did not snap" ;; esac
read -r X _ WW _ _ <<< "$G"
CX=$((${X%.*} + ${WW%.*} / 2))
N=$(((BX + 80 - CX) / 32 + 1))
[ "$N" -gt 0 ] || stage_fail "w's centre ($CX) is already past Virtual-1's left edge ($BX)"
activate w
emark
dmark
js 'workspace.slotWindowMove();'
sleep 0.5
echo "  keyboard move started on w: $(wprop w 'w.move') ($N Alt+Right arrows)"
ARGS=(keydown 56 wait 150)
for _ in $(seq "$N"); do
    ARGS+=(keydown 106 wait 15 keyup 106 wait 25)
done
ARGS+=(wait 1200 keyup 56 wait 600 keydown 1 wait 40 keyup 1)
fake "${ARGS[@]}"
sleep 2.5
A=$(geo w)
END=$(dlog "endDrag: [^ ]*$WU.*cancelled=.*" | tail -1)
ECX=$(echo "$END" | grep -o "cursor= [-0-9]*" | grep -o "[-0-9]*$")
echo "  ${END:-no endDrag line}"
[ "$(elog "Window move finished without button release, reverted= true" | wc -l)" -ge 1 ] \
    || stage_fail "KWin's move did not end reverted (no 'reverted= true' line)"
[ "$(dlog "activateSnapDragIfNeeded: promoting [^ ]*$WU" | wc -l)" -ge 1 ] \
    || stage_fail "the snap drag was never promoted to an active one"
echo "$END" | grep -q "cancelled= true" || stage_fail "endDrag did not arrive cancelled"
{ [ -n "$ECX" ] && [ "$ECX" -ge "$BX" ]; } || stage_fail "the endDrag cursor (${ECX:-?}) is not over Virtual-1 (x >= $BX)"
echo "  w $A"
check "cross-screen release lines for w" "$(dlog "Cross-screen drag: releasing.*$WU.*" | wc -l)" -eq 0
check "w's zone after the cancel (its zone 1)" "$(zoneof "$W")" = "$Z"
check "w on its zone-1 rect" "$(rect "$A")" = "$(rect "$G")"
finish
