#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: GPL-3.0-or-later
#
# f1170-shell-unmaps-after-drag: the daemon's per-screen overlay shell (an output-sized layer
# surface on the Overlay layer) is unmapped again once a snapping drag ends, so it no longer sits
# above fullscreen windows and blocks direct scanout. Runner: 1 output, flag F.
# Staging: Snap Assist and the zone selector off, Virtual-0 COLS3. w opens free and is dragged
#   with Meta+left, Alt (the snap trigger) pressed once the move is under way, then dropped.
#   Staging gates: no plasmazonesd window is mapped before the drag (the start-up prime has
#   finished), and one is mapped while the trigger is held (the overlay showed).
# Asserts: no plasmazonesd window is mapped 3 s after the drop.
# On the PR #1170 tree before its audit fix: the drop only idled the zone overlay
#   (WindowDragAdaptor::hideOverlayAndSelector -> setIdleForDragPause), which leaves the overlay
#   slot visible, so the shell stayed mapped for the rest of the session and the assert reads 1.
set -u
NEST="$1"
LOG="$2"
. "$(dirname "$0")/../lib.sh"
kwjs_ok
need_outputs 1
setting snapAssistEnabled false
setting zoneSelectorEnabled false
assign Virtual-0 "$COLS3"
sleep 1.5

# shellcount -> how many windows KWin lists for the daemon's overlay shell now. An unmapped layer
# surface is not in the list, so this is the number of mapped shells.
shellcount() {
    jsread SHELLN '(function () { var n = 0; const w = workspace.windowList(); for (var i = 0; i < w.length; i++) { if (String(w[i].resourceClass).indexOf("plasmazonesd") >= 0) { n++; } } return n; })()'
}

open w 400 300
sendto w Virtual-0 || stage_fail "w not on Virtual-0 ($(geo w))"
sleep 1
BEFORE=$(shellcount)
[ "$BEFORE" = 0 ] || stage_fail "a plasmazonesd window is already mapped before the drag ($BEFORE)"

read -r X Y W H _ <<< "$(geo w)"
CX=$((${X%.*} + ${W%.*} / 2))
CY=$((${Y%.*} + ${H%.*} / 2))
read -r SX SY SW SH <<< "$(scr Virtual-0)"
DX=$((SX + SW / 4))
DY=$((SY + SH / 2))
dmark
# Held, not dropped: press, start the move, press the trigger and move to the target.
fake abs "$CX" "$CY" wait 100 keydown 125 wait 50 down left wait 150 \
    keydown 56 wait 150 path "$CX" "$CY" "$DX" "$DY" 20 30 wait 900
DURING=$(shellcount)
fake up left wait 80 keyup 56 keyup 125
[ "$(dlog "initializeOverlay: screenId=.*" | wc -l)" -ge 1 ] \
    || stage_fail "no zone overlay showed during the drag, so the trigger did not take"
[ "$DURING" -ge 1 ] || stage_fail "no plasmazonesd window was mapped while the trigger was held ($DURING)"
echo "mapped shells: before $BEFORE, during the held drag $DURING"
sleep 3
AFTER=$(shellcount)
echo "mapped shells 3 s after the drop: $AFTER"
check "plasmazonesd windows mapped 3 s after the drop" "$AFTER" -eq 0
finish
