#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: GPL-3.0-or-later
#
# f1112-drag-off-removed-output: a window dragged while its output goes away settles its drop from
# the screen its evacuee verdict placed it on, not from the dead one. Runner: 2 outputs, flag F,
# PZ_F1112_B=snap (f1112-drag-off-removed-output-snap) or autotile
# (f1112-drag-off-removed-output-autotile).
# Staging: Virtual-1 (A) COLS3, Virtual-0 (B) COLS3 (snap) or autotile:bsp (autotile), Snap Assist
#   off. w opens on A. One fakeinput run in the background presses Meta and the left button on w,
#   moves it a little, holds for 7 s, then moves to a point on B and releases (Meta+left never
#   snaps: the trigger is Alt). While it holds, A is disabled and KWin evacuates w to B. The verdict
#   counts only when w's settle row reads verdict 2 and w followed the pointer to the drop point.
# Asserts: no WindowTracking.windowScreenChanged or windowCrossedScreens call naming w from the
#   drop on (start and landing are both B).
# A FAIL means: the held crossing settled from the dead screen.
set -u
NEST="$1"
LOG="$2"
. "$(dirname "$0")/../lib.sh"
BMODE="${PZ_F1112_B:-snap}"
kwjs_ok
need_outputs 2
[ -x "$PZR_FAKEINPUT" ] || stage_fail "no fakeinput binary (the probe needs flag F)"
read -r BX BY BW BH <<< "$(scr Virtual-0)"
case "$BMODE" in
snap) BL="$COLS3" ;;
autotile) BL="autotile:bsp" ;;
*) stage_fail "unknown PZ_F1112_B '$BMODE'" ;;
esac
on_exit 'ksd output.Virtual-1.enable >/dev/null'
setting snapAssistEnabled false
assign Virtual-1 "$COLS3"
assign Virtual-0 "$BL"
sleep 2
open w
sendto w Virtual-1 || stage_fail "w not on Virtual-1 ($(geo w))"
sleep 1
ids
WU=$(uuid w)
[ -n "$WU" ] || stage_fail "w never mapped"
G=$(geo w)
read -r GX GY GW GH _ <<< "$G"
CX=$((${GX%.*} + ${GW%.*} / 2))
CY=$((${GY%.*} + ${GH%.*} / 2))
DX=$((BX + 300))
DY=$((BY + BH - 250))
echo "staged: w $G | B $BX $BY ${BW}x$BH ($BMODE) | press at $CX,$CY, drop at $DX,$DY"
MON="$NEST/f1112-monitor.log"
mon_start "$MON" "type='method_call',interface='org.plasmazones.WindowTracking',member='windowScreenChanged'" \
    "type='method_call',interface='org.plasmazones.WindowTracking',member='windowCrossedScreens'"
mmark "$MON"
dmark
emark
fake abs "$CX" "$CY" wait 100 keydown 125 wait 50 down left wait 150 \
    path "$CX" "$CY" $((CX + 60)) $((CY + 40)) 6 30 wait 7000 \
    path $((BX + BW / 2)) $((BY + BH / 2)) "$DX" "$DY" 12 40 wait 400 up left wait 80 keyup 125 &
FIPID=$!
sleep 2
echo "== Virtual-1 disabled mid-drag"
ksd output.Virtual-1.disable
sleep 2
echo "  w while held: $(geo w)"
settlerows "$WU"
V=$(verdictof "$WU")
note "crossing calls naming w before the drop: $(named "$MON" windowScreenChanged "$WU") $(named "$MON" windowCrossedScreens "$WU")"
mmark "$MON"
wait "$FIPID"
sleep 2.5
A=$(geo w)
read -r AX AY AW AH _ <<< "$A"
ACX=$((${AX%.*} + ${AW%.*} / 2))
ACY=$((${AY%.*} + ${AH%.*} / 2))
echo "== dropped: w $A (centre $ACX,$ACY)"
[ "$V" = 2 ] || stage_fail "w's settle row is not verdict 2 (${V:-no row})"
{ [ $((ACX - DX)) -lt 200 ] && [ $((DX - ACX)) -lt 200 ] && [ $((ACY - DY)) -lt 200 ] && [ $((DY - ACY)) -lt 200 ]; } \
    || stage_fail "the drag did not survive the output removal (w is not at the drop point)"
msgs "$MON" | grep "$WU" | sed 's/org.pz.plain|[^ ]*/<w>/; s/^/    /'
check "crossing calls naming w since the drop" \
    "$(($(named "$MON" windowScreenChanged "$WU") + $(named "$MON" windowCrossedScreens "$WU")))" -eq 0
finish
