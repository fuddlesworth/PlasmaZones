#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: GPL-3.0-or-later
#
# fu-optout2-zone-selector: the drag zone selector stays hidden on a screen set to tile with no
# algorithm (autotile:none), and still shows on a snapping one. Runner: 2 outputs, flag F.
# Staging: Snap Assist off, the zone selector on at the top edge (position 1) with a 60 px trigger
#   distance. Virtual-1 (A) COLS3, Virtual-0 (B) desktop 1 autotile:none. w opens on A. Control: a
#   plain drag (Meta+left, no snap trigger: the selector is the no-trigger drag's surface, and a
#   held trigger dismisses it) takes w to A's top edge, holds, slides 160 px along it, holds, and
#   drops back in A's middle. Then the same drag on B's top edge, dropped in B's middle.
# Asserts: the selector shows on A (a staging gate: "selector hit-test: screen= Virtual-1" lines,
#   which the overlay logs only while the selector is visible); on B no such line names
#   Virtual-0; the drop on B commits no zone.
# On main (8ca183308): checkZoneSelectorTrigger force-hid the selector only on an engine-owned
#   screen, so it popped up on the opt-out too ("selector hit-test: screen= Virtual-0" lines),
#   where a drop on one of its zones would snap the window into a screen nothing manages.
set -u
NEST="$1"
LOG="$2"
. "$(dirname "$0")/../lib.sh"
kwjs_ok
need_outputs 2
setting snapAssistEnabled false
setting zoneSelectorEnabled true
echo "selector position: $(gsetting zoneSelectorPosition "<1>") | trigger distance: $(gsetting zoneSelectorTriggerDistance "<60>")"
assign Virtual-1 "$COLS3"
assignd Virtual-0 1 "autotile:none"
sleep 1.5
open w 300 300
sendto w Virtual-1 || stage_fail "w not on Virtual-1 ($(geo w))"
ids
WU=$(uuid w)
[ -n "$WU" ] || stage_fail "w never mapped"
W="org.pz.plain|$WU"
# edgedrag SCREEN: a plain Meta+left drag of w (no snap trigger: with the trigger held the zone
# overlay takes priority and the daemon dismisses the selector, so it is only offered on a drag
# without it) from its centre to SCREEN's top edge, held there, slid 160 px along the edge (so
# the selector's hit-test logs again), held, then dropped in SCREEN's middle
edgedrag() {
    local sx sy sw sh cx cy ex ey
    read -r sx sy sw sh <<< "$(scr "$1")"
    read -r cx cy <<< "$(centre w)"
    [ -n "$cx" ] || stage_fail "no frame for w to drag"
    ex=$((sx + sw / 2))
    ey=$((sy + 2))
    fake abs "$cx" "$cy" wait 100 keydown 125 wait 50 down left wait 150 \
        path "$cx" "$cy" "$ex" "$ey" 20 30 wait 1200 path "$ex" "$ey" $((ex + 160)) "$ey" 8 40 wait 1200 \
        path $((ex + 160)) "$ey" "$ex" $((sy + sh / 2)) 15 30 wait 600 up left wait 80 keyup 125
    sleep 2
}
echo "== control: the selector on A's top edge"
dmark
edgedrag Virtual-1
dlog "selector hit-test: .*" | head -4 | sed 's/^/    /'
[ "$(dlog "selector hit-test: screen= \"?Virtual-1\"?" | wc -l)" -ge 1 ] \
    || stage_fail "the zone selector did not show on Virtual-1's top edge, so the drag never reached it"
echo "  w after the control drop: $(wstat w "$W")"
echo "== the selector on B's (the opt-out's) top edge"
dmark
edgedrag Virtual-0
dlog "selector hit-test: .*" | head -4 | sed 's/^/    /'
check "selector hit-test lines on the opt-out Virtual-0" \
    "$(dlog "selector hit-test: screen= \"?Virtual-0\"?" | wc -l)" -eq 0
echo "  w after the drop on B: $(wstat w "$W")"
check "w's output after the drop on B" "$(outof "$(geo w)")" = Virtual-0
check "w's zone after the drop on B" "$(zoneof "$W")" = ""
check "isWindowFloating w after the drop on B" "$(floating "$W")" = false
finish
