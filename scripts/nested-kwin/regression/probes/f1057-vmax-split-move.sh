#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: GPL-3.0-or-later
#
# f1057-vmax-split-move: a vertically maximized window counts on the half of a split monitor it is
# on. Runner: 1 output, flag F (the keyboard move).
# Staging: Virtual-0 split left/right into Virtual-0/vs:0 and Virtual-0/vs:1, both COLS3, Snap
#   Assist off. A dbus-monitor records the effect's windowScreenChanged, activeWindowScreenChanged
#   and windowCrossedScreens calls.
#   Run 1: p1 in the left half, setMaximize(true, false), its frame moved right by half the output
#   with y and height kept (KWin keeps MaximizeVertical).
#   Run 2: p2 the same, moved with workspace.slotWindowMove() and Right arrows past the split, then
#   Return (KWin's own x-only interactive move).
#   Control: p3 fully maximized from the left half.
# Asserts, both runs: a call names the window with Virtual-0/vs:1, getWindowState's screenId is
#   Virtual-0/vs:1, and snap_to_zone_1 lands it in vs:1 (x at or past the split). Control: p3 reads
#   Virtual-0/vs:0.
# A FAIL means: the vertical maximize kept the window counted on the half it left.
set -u
NEST="$1"
LOG="$2"
. "$(dirname "$0")/../lib.sh"
kwjs_ok
read -r OX OY OW OH <<< "$(scr Virtual-0)"
[ -n "${OW:-}" ] || stage_fail "no Virtual-0 geometry"
SPLITX=$((OX + OW / 2))
setting snapAssistEnabled false
SPLIT='{"screens":[{"index":0,"displayName":"L","region":{"x":0,"y":0,"width":0.5,"height":1}},{"index":1,"displayName":"R","region":{"x":0.5,"y":0,"width":0.5,"height":1}}]}'
PZ org.plasmazones.Screen.setVirtualScreenConfig Virtual-0 "$SPLIT" >/dev/null
sleep 2
assign Virtual-0/vs:0 "$COLS3"
assign Virtual-0/vs:1 "$COLS3"
sleep 1.5
MON="$NEST/f1057-monitor.log"
mon_start "$MON" "type='method_call',member='windowScreenChanged'" "type='method_call',member='activeWindowScreenChanged'" \
    "type='method_call',member='windowCrossedScreens'"
# stage CAPTION: open, place in the left half, vertical maximize; sets STAGED to its uuid
stage() {
    open "$1"
    moveto "$1" $((OX + OW / 4 - 200)) $((OY + 150))
    sleep 1
    ids
    STAGED=$(uuid "$1")
    [ -n "$STAGED" ] || stage_fail "$1 never mapped"
    activate "$1"
    on "$1" 'w[i].setMaximize(true, false);'
    sleep 1.5
    local g
    g=$(geo "$1")
    echo "  $1 vertically maximized in the left half: $g | screenId $(wscreen "org.pz.plain|$STAGED")"
    [ "$(maxof "$g")" = 1 ] || stage_fail "setMaximize(true, false) did not leave $1 at max=1 ($g)"
}
# verdicts CAPTION UUID: the reads after a move
verdicts() {
    local g x
    g=$(geo "$1")
    echo "  $1 after the move: $g (split at $SPLITX)"
    [ "$(maxof "$g")" = 1 ] || stage_fail "$1 is no longer vertically maximized after the move ($g)"
    check "$1: calls naming it with Virtual-0/vs:1 since the move" "$(msgs "$MON" | grep "$2" | grep -c "Virtual-0/vs:1")" -ge 1
    check "$1: getWindowState screenId" "$(wscreen "org.pz.plain|$2")" = "Virtual-0/vs:1"
    activate "$1"
    shortcut snap_to_zone_1
    sleep 2
    g=$(geo "$1")
    read -r x _ _ _ <<< "$g"
    echo "  snap_to_zone_1: $g zone=$(zoneof "org.pz.plain|$2")"
    check "$1: snap_to_zone_1 lands it at or past the split x $SPLITX" "${x%.*}" -ge "$SPLITX"
}

echo "== run 1: scripted x-only move of a vertically maximized p1"
stage p1
P1U="$STAGED"
[ "$(wscreen "org.pz.plain|$P1U")" = "Virtual-0/vs:0" ] || stage_fail "p1 does not start on Virtual-0/vs:0"
mmark "$MON"
read -r X Y _ _ <<< "$(geo p1)"
moveto p1 $((${X%.*} + OW / 2)) "${Y%.*}"
sleep 2
verdicts p1 "$P1U"

echo "== run 2: keyboard move (slotWindowMove, Right arrows, Return) of a vertically maximized p2"
stage p2
P2U="$STAGED"
activate p2
js 'workspace.slotWindowMove();'
sleep 0.6
[ "$(wprop p2 'w.move')" = true ] || stage_fail "the interactive move of p2 did not start"
mmark "$MON"
N=$((OW / 2 / 8 + 2))
ARGS=()
for _ in $(seq "$N"); do ARGS+=(keydown 106 wait 12 keyup 106 wait 12); done
fake "${ARGS[@]}" wait 300 keydown 28 wait 40 keyup 28
sleep 2
verdicts p2 "$P2U"

echo "== control: a fully maximized p3 from the left half"
open p3
moveto p3 $((OX + OW / 4 - 200)) $((OY + 150))
sleep 1
ids
P3U=$(uuid p3)
[ -n "$P3U" ] || stage_fail "p3 never mapped"
on p3 'w[i].setMaximize(true, true);'
sleep 2
echo "  p3 $(geo p3)"
check "p3 (fully maximized from the left half) screenId" "$(wscreen "org.pz.plain|$P3U")" = "Virtual-0/vs:0"
finish
