#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: GPL-3.0-or-later
#
# f731-drag-between-tiling-monitors: a tile dragged from one tiling monitor to another is settled
# at the drop, released from its old engine before it floats on the new monitor. Runner: 2
# outputs, flag F.
# Staging: both outputs autotile:bsp (drag behaviour at its Float default). t1 and t2 tile on
#   Virtual-0, u1 alone on Virtual-1. t2 is Meta+left dragged (no trigger) onto the middle of
#   Virtual-1, held about 5.5 s (sampled 2.6 s into the drag), then dropped. A dbus-monitor records
#   the effect's Tiling.releaseWindowTracking and WindowTracking.setWindowFloatingForScreen calls.
# Asserts: u1 and t1 keep their frames mid-drag, Virtual-1's tiles do not change mid-drag, and no
#   "Window moved between monitors" line comes while the button is down; after the drop u1 is
#   unchanged, t1 has the whole of Virtual-0, t2 floats on Virtual-1 and is in neither monitor's
#   tiles, and on the wire releaseWindowTracking for t2 comes before setWindowFloatingForScreen
#   naming t2 and Virtual-1.
# A FAIL means: t2 was announced to Virtual-1 mid-drag (t1 grew and u1 shrank under the pointer),
#   or it was floated inside Virtual-0's engine before the release.
set -u
NEST="$1"
LOG="$2"
. "$(dirname "$0")/../lib.sh"
kwjs_ok
need_outputs 2
part() { echo "$1" | grep -o "$2 [^|]*" | sed 's/ *$//'; }
for s in Virtual-0 Virtual-1; do
    assign "$s" "autotile:bsp"
done
sleep 2
for c in t1 t2 u1; do
    open "$c" 300 300
    case "$c" in u*) out=Virtual-1 ;; *) out=Virtual-0 ;; esac
    sendto "$c" "$out" || stage_fail "$c not on $out ($(geo "$c"))"
done
sleep 1.5
read -r V0X V0Y V0W V0H <<< "$(scr Virtual-0)"
read -r V1X V1Y V1W V1H <<< "$(scr Virtual-1)"
ids
T2=$(uuid t2)
[ -n "$T2" ] || stage_fail "t2 never mapped"
ID="org.pz.plain|$T2"
PAIR0=$(frames t1 u1)
V1T0=$(tilelist Virtual-1)
echo "start: $PAIR0 | t2 $(geo t2) | Virtual-0 tiles $(tilelist Virtual-0) | Virtual-1 tiles $V1T0"
[ "$(outof "$(geo t2)")" = Virtual-0 ] || stage_fail "t2 left Virtual-0"
case "$(tilelist Virtual-0)" in *"${T2: -8}"*) ;; *) stage_fail "t2 is not one of Virtual-0's tiles" ;; esac
read -r GX GY GW GH _ <<< "$(geo t2)"
GX=$((${GX%.*} + ${GW%.*} / 2))
GY=$((${GY%.*} + ${GH%.*} / 2))
DX=$((V1X + V1W / 2))
DY=$((V1Y + V1H / 2))
MON="$NEST/f731-monitor.log"
mon_start "$MON" "type='method_call',interface='org.plasmazones.Tiling',member='releaseWindowTracking'" \
    "type='method_call',interface='org.plasmazones.WindowTracking',member='setWindowFloatingForScreen'"
mmark "$MON"
emark
echo "== t2: Meta+left drag from $GX,$GY to $DX,$DY, no trigger, held about 5.5 s"
fake abs "$GX" "$GY" wait 100 keydown 125 wait 50 down left wait 150 \
    path "$GX" "$GY" "$DX" "$DY" 20 30 wait 5500 up left wait 80 keyup 125 &
FIPID=$!
sleep 2.6
MID=$(frames t1 u1)
echo "mid-drag: $MID | t2 $(geo t2)"
check "mid-drag: u1 keeps its frame" "$(part "$MID" u1)" = "$(part "$PAIR0" u1)"
check "mid-drag: t1 keeps its frame" "$(part "$MID" t1)" = "$(part "$PAIR0" t1)"
check "mid-drag: Virtual-1's tiles unchanged" "$(tilelist Virtual-1)" = "$V1T0"
check "transfer lines for t2 while the button is down" "$(elog "Window moved between monitors: .*$T2" | wc -l)" -eq 0
wait "$FIPID"
sleep 2.5
AFTER=$(frames t1 u1)
echo "after the drop: $AFTER | t2 $(geo t2)"
check "after the drop: u1 keeps its frame" "$(part "$AFTER" u1)" = "$(part "$PAIR0" u1)"
check "after the drop: t1 has the whole of Virtual-0" "$(part "$AFTER" t1)" = "t1 $V0X,$V0Y ${V0W}x$V0H"
check "after the drop: t2 floating" "$(floating "$ID")" = true
check "after the drop: t2's state names Virtual-1" "$(wscreen "$ID")" = Virtual-1
check "after the drop: t2 not one of Virtual-0's tiles" "$(tilelist Virtual-0)" '!~' "${T2: -8}"
check "after the drop: t2 not one of Virtual-1's tiles" "$(tilelist Virtual-1)" '!~' "${T2: -8}"
ORDER=$(msgs "$MON" | grep "$T2" | awk '{ print $1 }' | paste -sd' ')
echo "  wire order for t2: $ORDER"
msgs "$MON" | grep "$T2" | sed 's/org.pz.plain|[^ ]*/<t2>/; s/^/    /'
check "wire: releaseWindowTracking for t2 comes first" "$(echo "$ORDER" | awk '{ print $1 }')" = releaseWindowTracking
check "wire: setWindowFloatingForScreen for t2 names Virtual-1 after the release" \
    "$(msgs "$MON" | grep "$T2" | awk 'r && $1 == "setWindowFloatingForScreen" && / Virtual-1 / { n++ } $1 == "releaseWindowTracking" { r = 1 } END { print n + 0 }')" -ge 1
finish
