#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: GPL-3.0-or-later
#
# f718-arrival-move-back: a window the daemon moves onto a tiling monitor is tracked there, so a
# KWin move back off it is seen. Runner: 2 outputs; variant x11 flags XFS
#   PZR_F718_CLIENT=x11 (an X11 client, whose move echo lands inside the apply), variant wayland
#   flags FS PZR_F718_CLIENT=wayland (a same-size Wayland move KWin applies at once).
# Staging: inner and outer gap 0, Snap Assist off. Virtual-0 Columns (2) snapping, Virtual-1
#   autotile:bsp with one tile t1. p1 snapped into zone 2 (the right half), so
#   move_window_right is the cross-mode move onto Virtual-1. Phase 1: the move, then a KWin script
#   sends p1 back to Virtual-0. Phase 2: p1 snapped again, moved right again, then Meta-dragged
#   inside Virtual-1 with fakeinput.
# Asserts: on the script move back the effect logs "Window moved between monitors: <p1>
#   Virtual-1 -> Virtual-0" and t1 has the whole of Virtual-1 again; after the phase 2 drag p1
#   floats with its pre-tile size restored at the drag start, and t1 has the whole of Virtual-1.
# A FAIL means: p1 stayed untracked on the effect side after the daemon's move, so its move back
#   went unseen and t1 kept half the monitor with a hole where p1 was.
set -u
NEST="$1"
LOG="$2"
. "$(dirname "$0")/../lib.sh"
kwjs_ok
need_outputs 2
read -r V1X V1Y V1W V1H <<< "$(scr Virtual-1)"
gsetting innerGap "<0>" >/dev/null
gsetting outerGap "<0>" >/dev/null
setting snapAssistEnabled false
ZONE2="{05b9766b-d2a1-49ab-94fa-35d3e31f33f9}"
assign Virtual-0 "$COLS2"
assign Virtual-1 "autotile:bsp"
sleep 2
send() {
    sendto "$1" "$2" || stage_fail "$1 did not reach $2 ($(geo "$1"))"
    sleep 0.5
}
open t1 400 300
send t1 Virtual-1
case "${PZR_F718_CLIENT:-}" in
x11) xopen p1 400 300 ;;
wayland) open p1 400 300 ;;
*) stage_fail "PZR_F718_CLIENT must be x11 or wayland" ;;
esac
send p1 Virtual-0
ids
P1=$(uuid p1)
[ -n "$P1" ] || stage_fail "p1 never mapped"
ID="org.pz.plain|$P1"
snapzone2() {
    PZ org.plasmazones.Snap.moveWindowToZone "$ID" "$ZONE2" >/dev/null
    sleep 2
    [ "$(zoneof "$ID")" = "$ZONE2" ] || stage_fail "p1 did not snap into Columns (2) zone 2"
}
WHOLE="$V1X $V1Y $V1W $V1H"
snapzone2
echo "p1 in Columns (2) zone 2: $(geo p1) | t1 $(geo t1)"

echo "== phase 1: the keyboard move, then a script move back"
activate p1
emark
shortcut move_window_right
sleep 2.5
echo "  landed: p1 $(geo p1) | t1 $(geo t1)"
[ "$(outof "$(geo p1)")" = Virtual-1 ] || stage_fail "move_window_right did not carry p1 onto Virtual-1"
emark
js "var o = null; for (var i = 0; i < workspace.screens.length; i++) { if (workspace.screens[i].name === \"Virtual-0\") { o = workspace.screens[i]; } } const w = workspace.windowList(); for (var j = 0; j < w.length; j++) { if (w[j].caption === \"p1\" && o) { workspace.sendClientToScreen(w[j], o); } }"
sleep 2.5
[ "$(outof "$(geo p1)")" = Virtual-0 ] || stage_fail "the script did not move p1 back to Virtual-0"
elog "Window moved between monitors: .*$P1" | head -1 | sed 's/^/    /'
check "transfer lines Virtual-1 -> Virtual-0 for p1 on the script move back" \
    "$(elog "Window moved between monitors: .*$P1" | grep -c '"Virtual-1" -> "Virtual-0"')" -eq 1
check "t1 after the move back" "$(rect "$(geo t1)")" = "$WHOLE"

echo "== phase 2: move right again, then a Meta+left drag inside Virtual-1"
snapzone2
activate p1
shortcut move_window_right
sleep 2.5
G=$(geo p1)
echo "  landed: p1 $G | t1 $(geo t1)"
[ "$(outof "$G")" = Virtual-1 ] || stage_fail "the second move_window_right did not carry p1 onto Virtual-1"
read -r GX GY GW GH _ <<< "$G"
CX=$((${GX%.*} + ${GW%.*} / 2))
CY=$((${GY%.*} + ${GH%.*} / 2))
emark
fake abs "$CX" "$CY" wait 100 keydown 125 wait 50 down left wait 150 \
    path "$CX" "$CY" $((CX + 150)) $((CY + 80)) 15 30 wait 400 up left wait 80 keyup 125
sleep 2.5
elog "Drag-start float.*$P1|Drag-to-float.*$P1|beginDrag reply.*$P1" | head -5 | sed 's/^/    /'
check "isWindowFloating p1 after the drag" "$(floating "$ID")" = true
check "drag-start float lines for p1 (its pre-tile size restored)" "$(elog "Drag-start float: .*$P1" | wc -l)" -ge 1
check "t1 after the drag" "$(rect "$(geo t1)")" = "$WHOLE"
finish
