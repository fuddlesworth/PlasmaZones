#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: GPL-3.0-or-later
#
# f1129-held-drag-unanswered-drop: a drag held from a live monitor still reports its move at an
# unanswered drop after the monitor it was over went away mid-drag. Runner: 3 outputs, flag F.
# Staging: Snap Assist off. Left to right: Virtual-0 (A) autotile:bsp holding tiles w and x,
#   Virtual-1 (C) COLS3, Virtual-2 (B) COLS3. One fakeinput run in the background presses Meta and
#   the left button on w and drags it across C onto B's right part, then holds (Meta+left never
#   snaps: the trigger is Alt). While it holds B is disabled: KWin moves w to C (settle verdict 2).
#   The nested daemon is frozen (dstop) and the button released, so the effect's endDrag reply
#   times out and the drop is Unanswered; the daemon runs again 2 s later. The verdict counts only
#   when the settle row reads verdict 2, w landed on C and the effect logged the endDrag timeout.
#   Escape cannot stand in for the timeout: the effect's keyboard grab takes it during a drag.
# Asserts: a WindowTracking.windowCrossedScreens call naming w with Virtual-0 after the release,
#   and none naming Virtual-2 (it has no output).
# A FAIL means: the held crossing was re-keyed to the screen it landed on, so A's engine kept w.
set -u
NEST="$1"
LOG="$2"
. "$(dirname "$0")/../lib.sh"
kwjs_ok
need_outputs 3
[ -x "$PZR_FAKEINPUT" ] || stage_fail "no fakeinput binary (the probe needs flag F)"
dpid >/dev/null || stage_fail "no gated nested daemon pid (the freeze the drop needs is not available)"
read -r BX BY BW BH <<< "$(scr Virtual-2)"
read -r CX0 CY0 CW CH <<< "$(scr Virtual-1)"
on_exit 'ksd output.Virtual-2.enable >/dev/null'
setting snapAssistEnabled false
assign Virtual-0 "autotile:bsp"
assign Virtual-1 "$COLS3"
assign Virtual-2 "$COLS3"
sleep 2
for c in w x; do
    open "$c"
    sendto "$c" Virtual-0 || stage_fail "$c not on Virtual-0 ($(geo "$c"))"
done
sleep 2
ids
WU=$(uuid w)
[ -n "$WU" ] || stage_fail "w never mapped"
G=$(geo w)
echo "staged: w $G | x $(geo x) | A tiles $(tiles Virtual-0) | C $CX0 $CY0 ${CW}x$CH | B $BX $BY ${BW}x$BH"
case "$(tiles Virtual-0)" in 2*) ;; *) stage_fail "Virtual-0 does not hold two tiles" ;; esac
read -r GX GY GW GH _ <<< "$G"
PX=$((${GX%.*} + ${GW%.*} / 2))
PY=$((${GY%.*} + ${GH%.*} / 2))
DX=$((BX + BW - 200))
DY=$((BY + BH / 2))
MON="$NEST/f1129-monitor.log"
mon_start "$MON" "type='method_call',interface='org.plasmazones.WindowTracking',member='windowCrossedScreens'" \
    "type='method_call',interface='org.plasmazones.WindowTracking',member='windowScreenChanged'"
mmark "$MON"
dmark
emark
echo "== drag of w from $PX,$PY across C to $DX,$DY on B, held"
fake abs "$PX" "$PY" wait 100 keydown 125 wait 50 down left wait 150 \
    path "$PX" "$PY" $((PX + 60)) $((PY + 40)) 6 30 \
    path $((PX + 60)) $((PY + 40)) "$DX" "$DY" 24 40 wait 9000 up left wait 80 keyup 125 &
FIPID=$!
sleep 3
echo "  w while held over B: $(geo w)"
echo "== Virtual-2 (B) disabled mid-drag"
ksd output.Virtual-2.disable
sleep 2.5
H=$(geo w)
echo "  w while held: $H"
settlerows "$WU"
V=$(verdictof "$WU")
echo "== daemon frozen, button released (the endDrag reply cannot come)"
mmark "$MON"
dstop || stage_fail "the daemon could not be frozen"
wait "$FIPID"
sleep 2
dcont
sleep 3
echo "  w dropped: $(geo w)"
[ "$V" = 2 ] || stage_fail "w's settle row is not verdict 2 (${V:-no row})"
[ "$(outof "$H")" = Virtual-1 ] || stage_fail "KWin did not move w to C (Virtual-1) at the removal ($H)"
[ "$(elog "endDrag timed out" | wc -l)" -ge 1 ] || stage_fail "no endDrag timeout; the drop was answered"
msgs "$MON" | grep "$WU" | sed 's/org.pz.plain|[^ ]*/<w>/; s/^/    /'
check "windowCrossedScreens naming w with Virtual-0 since the release" \
    "$(msgs "$MON" | grep "^windowCrossedScreens " | grep "$WU" | grep -c "Virtual-0")" -ge 1
check "windowCrossedScreens naming w with Virtual-2 since the release" \
    "$(msgs "$MON" | grep "^windowCrossedScreens " | grep "$WU" | grep -c "Virtual-2")" -eq 0
note "A tiles after the daemon resumed: $(tiles Virtual-0) | x $(geo x)"
finish
