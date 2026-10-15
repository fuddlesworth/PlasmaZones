#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: GPL-3.0-or-later
#
# f1071-grab-released-daemon-gone: the drag's keyboard grab goes when the daemon leaves the bus.
# Runner: 1 output, flag F.
# Staging: Virtual-0 COLS3, Snap Assist off, w (plain_gtk.py) free at 200,150. One fakeinput run in
#   the background: a Meta+left drag of w with Alt (the default snap trigger) held, so the drag is
#   activated and the effect takes its keyboard grab. 1.5 s in, the nested daemon is stopped with its
#   bus activation blocked (dkill). About 6 s in, Escape with the button still held; KWin's move state
#   is read 0.7 s later; then the button and keys come up. The daemon is started again at the end.
# Asserts: KWin's move has ended right after Escape (move=false), and after the release w is back at
#   its start frame.
# A FAIL means: the effect's grab still swallowed Escape and the release dropped w where the pointer
#   left it.
set -u
NEST="$1"
LOG="$2"
. "$(dirname "$0")/../lib.sh"
kwjs_ok
[ -x "$PZR_FAKEINPUT" ] || stage_fail "no fakeinput binary (the probe needs flag F; run.sh builds it)"
setting snapAssistEnabled false
assign Virtual-0 "$COLS3"
sleep 1.5
open w
moveto w 200 150
sleep 1.5
G0=$(geo w)
echo "staged: w $G0"
[ "$(rect "$G0" | awk '{ print $1, $2 }')" = "200 150" ] || stage_fail "w did not move to 200,150"
dpid >/dev/null || stage_fail "no gated nested daemon pid before the drag"
read -r X Y WW HH _ <<< "$G0"
CX=$((${X%.*} + ${WW%.*} / 2))
CY=$((${Y%.*} + ${HH%.*} / 2))
activate w
emark
fake abs "$CX" "$CY" wait 100 keydown 125 wait 50 down left wait 150 keydown 56 wait 100 \
    path "$CX" "$CY" $((CX + 160)) $((CY + 90)) 12 30 wait 5600 keydown 1 wait 40 keyup 1 wait 1800 \
    up left wait 50 keyup 56 wait 50 keyup 125 &
FIPID=$!
sleep 1.5
echo "  mid-drag before the stop: $(geo w)"
dkill
echo "  nested daemon stopped with the button held"
# Escape lands about 6.2 s after the gesture began; read 0.7 s after it.
sleep 4.2
MV=$(wprop w 'w.move + " " + w.frameGeometry.x + "," + w.frameGeometry.y')
wait "$FIPID"
sleep 1.5
A=$(geo w)
echo "  KWin move state right after Escape: ${MV:-unread} | w after the release $A"
[ -n "$MV" ] || stage_fail "KWin's move state was not read after Escape"
check "KWin's move state right after Escape" "${MV%% *}" = false
check "w after the release, at its start frame" "$(rect "$A")" = "$(rect "$G0")"
drestart
sleep 2
finish
