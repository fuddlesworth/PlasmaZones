#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: GPL-3.0-or-later
#
# f751-crossing-after-move-end: a drag whose drop goes unanswered reports its crossing onto the
# other monitor when KWin's move ends, not while it still runs. Runner: 2 outputs, flag F.
# Staging: the snapping drag trigger set to the right mouse button alone (Snapping.Behavior
#   Triggers in the nested config.json, then reloadSettings), both outputs COLS3, Snap Assist off.
#   f1 is Meta+left dragged, the right button pressed to activate snapping and the left let go,
#   then dragged onto Virtual-1 and dropped. The daemon is frozen for the whole tail, so the drop
#   goes unanswered (the effect's timeout). A dbus-monitor stamps the windowScreenChanged calls
#   while KWin scripts poll whether the move still runs.
# Asserts: f1 ends on Virtual-1, a windowScreenChanged call is sent, and the first one is stamped no
#   earlier than half a second before the last poll that saw the move running.
# A FAIL means: the crossing was reported mid-move (seconds before the move ended).
set -u
NEST="$1"
LOG="$2"
. "$(dirname "$0")/../lib.sh"
kwjs_ok
need_outputs 2
setting snapAssistEnabled false
sleep 1
CFG="$XDG_CONFIG_HOME/plasmazones/config.json"
[ -f "$CFG" ] || stage_fail "no nested config.json to set the drag trigger in"
python3 -c '
import json, sys
path = sys.argv[1]
data = json.load(open(path))
data.setdefault("Snapping", {}).setdefault("Behavior", {})["Triggers"] = [{"modifier": 0, "mouseButton": 2}]
json.dump(data, open(path, "w"))' "$CFG" || stage_fail "could not write the drag trigger"
PZ org.plasmazones.Settings.reloadSettings >/dev/null
assign Virtual-0 "$COLS3"
assign Virtual-1 "$COLS3"
sleep 2
open f1 300 300
moveto f1 600 300
sleep 1
MON="$NEST/f751-monitor.log"
mon_start "$MON" "type='method_call',interface='org.plasmazones.WindowTracking',member='windowScreenChanged'"
dpid >/dev/null || stage_fail "no gated nested daemon to freeze"
fake abs 750 450 wait 100 keydown 125 wait 50 down left wait 150 \
    path 750 450 520 400 15 30 wait 200 down right wait 400 path 520 400 512 384 4 30 wait 300 \
    up left wait 300 path 512 384 1500 384 25 30 wait 900 up right wait 80 keyup 125 &
FIPID=$!
on_exit "kill $FIPID 2>/dev/null"
sleep 1.95
dstop || stage_fail "the daemon could not be frozen"
# The poll keeps the old probe's pace (a 0.2 s settle after each script, not js's 0.4 s), so the
# last time it saw the move running sits close to the move's real end.
LAST_TRUE=""
for _ in $(seq 60); do
    kill -0 "$FIPID" 2>/dev/null || break
    nonce="$RANDOM$RANDOM"
    kwjs "const w = workspace.windowList(); for (var i = 0; i < w.length; i++) { if (w[i].caption === \"f1\") { print(\"MOVEST $nonce \" + w[i].move); } }"
    sleep 0.2
    NOW=$(date +%s.%N)
    grep -q "js: MOVEST $nonce true" "$LOG" && LAST_TRUE=$NOW
done
kill -0 "$FIPID" 2>/dev/null && stage_fail "the drag never finished"
echo "move last seen running at ${LAST_TRUE:-never}"
[ -n "$LAST_TRUE" ] || stage_fail "no poll saw the move running while the daemon was frozen"
sleep 0.5
dcont
sleep 3
G=$(geo f1)
echo "f1 after the drop: $G"
[ "$(outof "$G")" = Virtual-1 ] || stage_fail "the drag did not end on Virtual-1 ($G)"
N=$(grep -c "member=windowScreenChanged" "$MON")
check "windowScreenChanged calls sent" "$N" -ge 1
REPORT=$(grep "member=windowScreenChanged" "$MON" | head -1 | grep -o "time=[0-9.]*" | cut -d= -f2)
echo "first crossing report at ${REPORT:-none}"
if [ -n "$REPORT" ]; then
    D=$(python3 -c 'import sys; print(int(round((float(sys.argv[1]) - float(sys.argv[2])) * 1000)))' "$REPORT" "$LAST_TRUE")
    check "first crossing report relative to the last poll that saw the move running (ms)" "$D" -ge -500
fi
finish
