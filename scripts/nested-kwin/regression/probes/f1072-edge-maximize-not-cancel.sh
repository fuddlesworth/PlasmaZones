#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: GPL-3.0-or-later
#
# f1072-edge-maximize-not-cancel: an electric-edge maximize at the end of a keyboard move onto
# another output is not a cancel. Runner: 2 outputs, flag F.
# Staging: Virtual-0 autotile:monocle with m3, m2, m1 (all maximized), Virtual-1 COLS3, the default
#   autotile drag behaviour, Snap Assist off, KWin's default ElectricBorderMaximize. A dbus-monitor
#   records Tiling.retile calls.
#   Run 1: workspace.slotWindowMove() on m1, the pointer walks onto Virtual-1's centre and up to its
#   top edge (held 1.2 s for the maximize outline), Return.
#   Control: a keyboard move of m2 on Virtual-0, two Right arrows, Escape.
# Asserts: run 1 logs reverted= false, no cancelled endDrag for m1, no retile of Virtual-0, and m1
#   ends maximized on Virtual-1. Control: reverted= true, a cancelled endDrag for m2 and a retile of
#   Virtual-0.
# A FAIL means: the edge maximize read as a cancel and the source was retiled.
set -u
NEST="$1"
LOG="$2"
. "$(dirname "$0")/../lib.sh"
kwjs_ok
need_outputs 2
read -r BX BY BW BH <<< "$(scr Virtual-1)"
setting snapAssistEnabled false
assign Virtual-0 "autotile:monocle"
assign Virtual-1 "$COLS3"
sleep 2
for c in m3 m2 m1; do
    open "$c"
    sendto "$c" Virtual-0 || stage_fail "$c not on Virtual-0"
done
sleep 2
ids
M1U=$(uuid m1)
M2U=$(uuid m2)
[ -n "$M1U" ] && [ -n "$M2U" ] || stage_fail "m1 or m2 never mapped"
activate m1
echo "staged: m1 $(geo m1) | m2 $(geo m2) | m3 $(geo m3)"
[ "$(maxof "$(geo m1)")" = 3 ] && [ "$(maxof "$(geo m2)")" = 3 ] || stage_fail "no Monocle (m1 and m2 not max=3)"
MON="$NEST/f1072-monitor.log"
mon_start "$MON" "type='method_call',member='retile'"
# kbmove CAPTION: start KWin's keyboard move on CAPTION
kbmove() {
    activate "$1"
    js 'workspace.slotWindowMove();'
    sleep 0.5
    [ "$(wprop "$1" 'w.move')" = true ] || stage_fail "the keyboard move of $1 did not start"
}
reverted() { elog "Window move finished without button release, reverted= [a-z]*" | head -1 | grep -o "reverted= [a-z]*" | cut -d' ' -f2; }

echo "== run 1: keyboard move of m1 onto Virtual-1's top edge, Return"
read -r X Y W H _ <<< "$(geo m1)"
CX=$((${X%.*} + ${W%.*} / 2))
CY=$((${Y%.*} + ${H%.*} / 2))
mmark "$MON"
emark
kbmove m1
TX=$((BX + BW / 2))
fake path "$CX" "$CY" "$TX" $((BY + BH / 2)) 20 30 wait 300 \
    path "$TX" $((BY + BH / 2)) "$TX" "$BY" 15 30 wait 1200 keydown 28 wait 40 keyup 28
sleep 3
A=$(geo m1)
echo "  m1 $A | m2 $(geo m2) | m3 $(geo m3)"
if [ "$(outof "$A")" = Virtual-1 ] && [ "$(maxof "$A")" != 3 ]; then
    stage_fail "m1 is on Virtual-1 but not maximized: KWin's edge maximize did not trigger"
fi
check "run 1: the move-finished line's reverted" "$(reverted)" = false
check "run 1: cancelled endDrag lines for m1" "$(elog "endDrag cancelled: re-tiling [^ ]* for [^ ]*$M1U" | wc -l)" -eq 0
check "run 1: retile calls naming Virtual-0" "$(msgs "$MON" | grep -c "^retile Virtual-0")" -eq 0
check "run 1: m1 maximized on Virtual-1" "$(maxof "$A")@$(outof "$A")" = "3@Virtual-1"

echo "== control: keyboard move of m2 on Virtual-0, Escape"
sleep 1
mmark "$MON"
emark
kbmove m2
fake keydown 106 wait 20 keyup 106 wait 40 keydown 106 wait 20 keyup 106 wait 300 keydown 1 wait 40 keyup 1
sleep 3
echo "  m2 $(geo m2)"
check "control: the move-finished line's reverted" "$(reverted)" = true
check "control: cancelled endDrag lines for m2" "$(elog "endDrag cancelled: re-tiling [^ ]* for [^ ]*$M2U" | wc -l)" -eq 1
check "control: retile calls naming Virtual-0" "$(msgs "$MON" | grep -c "^retile Virtual-0")" -eq 1
finish
