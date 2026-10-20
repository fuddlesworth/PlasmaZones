#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: GPL-3.0-or-later
#
# f773-verbs-skip-dragged: placement shortcuts and bus verbs leave a window the user is dragging
# alone. Runner: 1 output, flag F.
# Staging: one output on COLS3, Snap Assist off, p1 free at 60,60, p2 snapped into zone 3. Two runs
#   of a Meta+left drag of p1 (no trigger) that carries it to 800,450, holds about 6 s and drops;
#   1.9 s into the drag:
#   run 1: shortcuts toggle_window_float, snap_to_zone_1, move_window_right and focus_zone_right;
#   run 2: bus calls Snap.moveWindowToZone p1 zone 2, Snap.toggleFloatForWindow p1 Virtual-0 and
#          Snap.snapToZoneByNumber 1 "".
# Asserts: after each drop p1 sits where the drag left it, no longer moving, with no zone and still
#   floating; run 1 logs one "shortcut: ignored, <p1> is being dragged" line for each placing
#   shortcut (three, focus_zone_right is not one), run 2 logs "Bus snap of <p1> refused: it is
#   being dragged", "Bus float of <p1> refused: it is being dragged" and "Bus verb refused: <p1>
#   is being dragged".
# A FAIL means: a verb placed the dragged window, so it ended on a zone rect or with a zone
#   committed under a free window.
set -u
NEST="$1"
LOG="$2"
. "$(dirname "$0")/../lib.sh"
kwjs_ok
setting snapAssistEnabled false
assign Virtual-0 "$COLS3"
sleep 2
for c in p2 p1; do open "$c" 300 300; done
ids
P1U=$(uuid p1)
P2U=$(uuid p2)
[ -n "$P1U" ] && [ -n "$P2U" ] || stage_fail "p1 or p2 never mapped"
P1="org.pz.plain|$P1U"
P2="org.pz.plain|$P2U"
Z2=$(zoneid "$COLS3" 2)
Z3=$(zoneid "$COLS3" 3)
[ -n "$Z2" ] && [ -n "$Z3" ] || stage_fail "COLS3's zone ids did not resolve"
PZ org.plasmazones.Snap.moveWindowToZone "$P2" "$Z3" >/dev/null
sleep 1.5
case "$(zoneof "$P2")" in "{"*) ;; *) stage_fail "p2 did not snap into zone 3" ;; esac
dragstate() { echo "$(geo p1) move=$(wprop p1 'w.move') active=$(activecap) zone='$(zoneof "$P1")' floating=$(floating "$P1")"; }
reset() {
    on p1 "w[i].frameGeometry = {x: 60, y: 60, width: 300, height: 300}; workspace.activeWindow = w[i];"
    sleep 1.5
}
# drag: grab p1's centre (210,210), carry it to 800,450, hold about 6 s, drop. Runs in the
# background; FIPID is the tool's pid. The hold has to outlast the mid-drag read and the verbs
# after it: every KWin-script read costs about 1.1 s.
drag() {
    fake abs 210 210 wait 100 keydown 125 wait 50 down left wait 150 \
        path 210 210 800 450 20 30 wait 6000 up left wait 80 keyup 125 &
    FIPID=$!
}
# midread: ONE read, about 1.9 s into the drag, of whether KWin is moving p1 and its frame then;
# sets MV (true/false) and MID ("x y w h"). A read per field would land after the drop.
midread() {
    local r
    r=$(wprop p1 'w.move + " " + w.frameGeometry.x + " " + w.frameGeometry.y + " " + w.frameGeometry.width + " " + w.frameGeometry.height')
    MV=${r%% *}
    MID=${r#* }
    echo "mid-drag: move=$MV frame $MID"
}
# landed RUN: the checks after a drop, against the frame p1 had mid-drag
landed() {
    local g
    g=$(geo p1)
    echo "p1 after the drop: $(dragstate)"
    check "run $1: p1's frame after the drop is the one it was dragged to" "$(rect "$g")" = "$(rect "$MID")"
    check "run $1: p1 no longer moving after the drop" "$(wprop p1 'w.move')" = false
    check "run $1: p1's zone after the drop" "$(zoneof "$P1")" = ""
    check "run $1: p1 floating after the drop" "$(floating "$P1")" = true
}

echo "== run 1: shortcuts during the drag"
reset
echo "p1 before: $(dragstate)"
{ [ "$(zoneof "$P1")" = "" ] && [ "$(floating "$P1")" = true ]; } || stage_fail "p1 is not free before the drag"
dmark
drag
sleep 1.9
midread
[ "$MV" = true ] || stage_fail "KWin is not moving p1 mid-drag"
for s in toggle_window_float snap_to_zone_1 move_window_right focus_zone_right; do
    shortcut "$s"
    sleep 0.25
done
wait "$FIPID"
sleep 2
landed 1
dlog "shortcut: ignored, [^ ]*$P1U[^ ]* is being dragged" | sed 's/^/    /'
# Three placing shortcuts; focus_zone_right places nothing and is answered, so it adds no line.
check "run 1: 'shortcut: ignored, <p1> is being dragged' lines" \
    "$(dlog "shortcut: ignored, [^ ]*$P1U[^ ]* is being dragged" | wc -l)" -eq 3

echo "== run 2: bus calls during the drag"
reset
echo "p1 before: $(dragstate)"
{ [ "$(zoneof "$P1")" = "" ] && [ "$(floating "$P1")" = true ]; } || stage_fail "p1 is not free before the second drag"
dmark
drag
sleep 1.9
midread
[ "$MV" = true ] || stage_fail "KWin is not moving p1 mid-drag in run 2"
PZ org.plasmazones.Snap.moveWindowToZone "$P1" "$Z2" >/dev/null
PZ org.plasmazones.Snap.toggleFloatForWindow "$P1" Virtual-0 >/dev/null
PZ org.plasmazones.Snap.snapToZoneByNumber 1 "" >/dev/null
wait "$FIPID"
sleep 2
landed 2
check "run 2: 'Bus snap of <p1> refused: it is being dragged' lines" \
    "$(dlog "Bus snap of [^ ]*$P1U[^ ]* refused: it is being dragged" | wc -l)" -eq 1
check "run 2: 'Bus float of <p1> refused: it is being dragged' lines" \
    "$(dlog "Bus float of [^ ]*$P1U[^ ]* refused: it is being dragged" | wc -l)" -eq 1
check "run 2: 'Bus verb refused: <p1> is being dragged' lines" \
    "$(dlog "Bus verb refused: [^ ]*$P1U[^ ]* is being dragged" | wc -l)" -eq 1
finish
