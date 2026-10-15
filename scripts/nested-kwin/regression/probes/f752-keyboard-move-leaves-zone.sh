#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: GPL-3.0-or-later
#
# f752-keyboard-move-leaves-zone: KWin's keyboard move of a snapped window, accepted with Return,
# takes it out of its zone; one cancelled with Escape keeps it snapped. Covers F1010 too.
# Runner: 1 output, flag F.
# Staging: Virtual-0 COLS3, Snap Assist off. f1 and f2 open and are moved by Snap.moveWindowToZone
#   into zones 1 and 3. f1 is keyboard-moved (workspace.slotWindowMove, Right x10, Return); f2 the
#   same with Escape.
# Asserts: f1 has no zone, sits right of zone 1's x at its free size; the effect logs the
#   Return as "reverted= false" and the Escape as "reverted= true"; f2 is back at zone 3's rect
#   and still in zone 3.
# A FAIL means: the Return left f1 mid-move or still filed in zone 1 while it sat off it.
set -u
NEST="$1"
LOG="$2"
. "$(dirname "$0")/../lib.sh"
kwjs_ok
setting snapAssistEnabled false
assign Virtual-0 "$COLS3"
sleep 2
open f1 300 300
FREE=$(size "$(geo f1)")
open f2 300 300
ids
F1="org.pz.plain|$(uuid f1)"
F2="org.pz.plain|$(uuid f2)"
Z1=$(zoneid "$COLS3" 1)
Z3=$(zoneid "$COLS3" 3)
PZ org.plasmazones.Snap.moveWindowToZone "$F1" "$Z1" >/dev/null
PZ org.plasmazones.Snap.moveWindowToZone "$F2" "$Z3" >/dev/null
sleep 2
G1=$(geo f1)
G2=$(geo f2)
echo "f1 snapped: $G1 | f2 snapped: $G2"
[ "$(zoneof "$F1")" = "$Z1" ] && [ "$(zoneof "$F2")" = "$Z3" ] || stage_fail "f1 or f2 did not snap"
read -r Z1X _ <<< "$G1"
Z3R=$(rect "$G2")
kmove() {
    activate "$1"
    sleep 0.5
    js 'workspace.slotWindowMove();'
    sleep 0.5
}
# KEY_RIGHT 106, KEY_ENTER 28, KEY_ESC 1
KEYS=()
for _ in $(seq 10); do KEYS+=(keydown 106 wait 40 keyup 106 wait 60); done
emark
kmove f1
fake "${KEYS[@]}" keydown 28 wait 40 keyup 28
sleep 2
G1=$(geo f1)
echo "f1 after Right x10 and Return: $G1"
read -r X _ <<< "$G1"
check "the Return is a finished move" "$(elog 'Window move finished without button release, reverted= false' | wc -l)" -ge 1
check "f1's zone after the Return" "$(zoneof "$F1")" = ""
check "f1 moved right of zone 1's x" "${X%.*}" -gt "${Z1X%.*}"
check "f1's size after the Return (its free size)" "$(size "$G1")" = "$FREE"
emark
kmove f2
fake "${KEYS[@]}" keydown 1 wait 40 keyup 1
sleep 2
G2=$(geo f2)
echo "f2 after Right x10 and Escape: $G2"
check "the Escape is a reverted move" "$(elog 'Window move finished without button release, reverted= true' | wc -l)" -ge 1
check "f2's zone after the Escape" "$(zoneof "$F2")" = "$Z3"
check "f2 back at zone 3's rect" "$(rect "$G2")" = "$Z3R"
finish
