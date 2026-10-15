#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: GPL-3.0-or-later
#
# fu-optout2-drag-overlay: a snap-trigger drag over a screen set to tile with no algorithm
# (autotile:none) draws no zone overlay there, and the drop commits no zone. Runner: 2 outputs,
# flag F. A regression row: it reads the same on main and on the fix.
# Staging: Snap Assist and the zone selector off. Virtual-1 (A) COLS3, Virtual-0 (B) desktop 1
#   autotile:none. w opens free on A and is dragged with Meta+left, Alt (the snap trigger) pressed
#   once the move is under way, onto the middle of B and dropped there. A staging gate checks that
#   the autotile engine's set does not hold B, so the overlay's own opt-out gate is what keeps B's
#   overlay away, not the excluded-screen set.
# Asserts: the overlay shows on A during the drag (a staging gate: the trigger took), and no
#   "initializeOverlay: screenId= Virtual-0" line is logged at all; after the drop w is on B with
#   no zone, not floating, and where it was dropped.
# On main (8ca183308): the same. OverlayService's autotile resolver was isAutotileScreen, which is
#   ScreenModeRouter::isAutotileMode, and the router's opt-out arm already answers Autotile for
#   autotile:none, so isSnappingContextInactive held B out there too. The optout2 change to
#   currentModeFor gives the same answer for every screen. A drag that starts on B does not tell
#   the builds apart either: beginDrag bypasses an engine-mode screen, so no overlay is asked for.
set -u
NEST="$1"
LOG="$2"
. "$(dirname "$0")/../lib.sh"
kwjs_ok
need_outputs 2
setting snapAssistEnabled false
setting zoneSelectorEnabled false
assign Virtual-1 "$COLS3"
assignd Virtual-0 1 "autotile:none"
sleep 1.5
[ "$(modeof Virtual-0 1)" = 1 ] || stage_fail "B's desktop 1 does not read as tiling after the opt-out assignment"
open w 300 300
sendto w Virtual-1 || stage_fail "w not on Virtual-1 ($(geo w))"
ids
WU=$(uuid w)
[ -n "$WU" ] || stage_fail "w never mapped"
W="org.pz.plain|$WU"
read -r BX BY BW BH <<< "$(scr Virtual-0)"
DX=$((BX + BW / 2))
DY=$((BY + BH / 2))
echo "staged: $(wstat w "$W") | drop point $DX,$DY on Virtual-0"
ENGSET=$(dlog "Updated autotile screens= .*" | tail -1)
[ -n "$ENGSET" ] || stage_fail "no autotile engine set was logged"
case "$ENGSET" in *Virtual-0*) stage_fail "the autotile engine set holds the opt-out ($ENGSET)" ;; esac
dmark
altdrag w "$DX" "$DY"
sleep 1
dlog "initializeOverlay: screenId=.*" | sed 's/^/    /' | head -6
[ "$(dlog "initializeOverlay: screenId= \"?Virtual-1\"?" | wc -l)" -ge 1 ] \
    || stage_fail "no snap overlay showed on Virtual-1 during the drag, so the trigger did not take"
check "overlay shown on the opt-out Virtual-0 during the drag" \
    "$(dlog "initializeOverlay: screenId= \"?Virtual-0\"?" | wc -l)" -eq 0
G=$(geo w)
echo "after the drop: $(wstat w "$W")"
check "w's output after the drop" "$(outof "$G")" = Virtual-0
check "w's zone after the drop" "$(zoneof "$W")" = ""
check "isWindowFloating w after the drop" "$(floating "$W")" = false
read -r CX CY <<< "$(centre w)"
check "w's centre x is where it was dropped (within 60 px)" "$(((CX - DX) * (CX - DX) <= 3600 ? 1 : 0))" -eq 1
check "w's centre y is where it was dropped (within 60 px)" "$(((CY - DY) * (CY - DY) <= 3600 ? 1 : 0))" -eq 1
check "B's mode for desktop 1 (the opt-out still reads as tiling)" "$(modeof Virtual-0 1)" = 1
finish
