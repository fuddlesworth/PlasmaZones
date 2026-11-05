#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: GPL-3.0-or-later
#
# f736-virtual-screen-drag-tracked: a window dragged across virtual screens onto a tiling half is
# tracked there, so a later move off that half transfers it. Runner: 1 output, flags FS.
# Staging: Snap Assist off. p1 and t1 are placed free, p1 in the left half and t1 in the right,
#   then Virtual-0 is split 0.5/0.5: vs:0 COLS3, vs:1 autotile:bsp (t1 tiles over it). p1 is
#   snapped into zone 1 on vs:0, then Meta+left dragged with Alt pressed once the move is under way
#   onto the middle of vs:1 and dropped there, where vs:1 takes it as a tile. Then a KWin script
#   moves p1 back into vs:0.
# Asserts: the script move logs one "Window moved between monitors: <p1> Virtual-0/vs:1 ->
#   Virtual-0/vs:0", and vs:1's tiles no longer list p1.
# A FAIL means: the drag's virtual-screen crossing was dropped, so p1 sat on vs:1 untracked by the
#   effect and the move back ran no transfer.
set -u
NEST="$1"
LOG="$2"
. "$(dirname "$0")/../lib.sh"
kwjs_ok
setting snapAssistEnabled false
for c in p1 t1; do
    open "$c" 300 300
done
place p1 150 150
place t1 1050 150
SPLIT='{"screens":[{"index":0,"displayName":"L","region":{"x":0,"y":0,"width":0.5,"height":1}},{"index":1,"displayName":"R","region":{"x":0.5,"y":0,"width":0.5,"height":1}}]}'
echo "split: '$(PZ org.plasmazones.Screen.setVirtualScreenConfig Virtual-0 "$SPLIT")'"
sleep 2
assign Virtual-0/vs:0 "$COLS3"
assign Virtual-0/vs:1 "autotile:bsp"
sleep 3
ids
P1=$(uuid p1)
[ -n "$P1" ] || stage_fail "p1 never mapped"
ID="org.pz.plain|$P1"
PZ org.plasmazones.Snap.moveWindowToZone "$ID" "$(zoneid "$COLS3" 1)" >/dev/null
sleep 2
ZN=$(zoneof "$ID")
echo "p1 snapped on vs:0: $(geo p1) zone='$ZN' | t1 $(geo t1) | vs:1 tiles $(tilelist Virtual-0/vs:1)"
case "$ZN" in "{"*) ;; *) stage_fail "p1 has no zone on vs:0 ($ZN)" ;; esac
read -r GX GY GW GH _ <<< "$(geo p1)"
GX=$((${GX%.*} + ${GW%.*} / 2))
GY=$((${GY%.*} + ${GH%.*} / 2))
echo "== Alt-drag of p1 from $GX,$GY to 1200,450 (vs:1's middle)"
fake abs "$GX" "$GY" wait 100 keydown 125 wait 50 down left wait 150 \
    keydown 56 wait 150 path "$GX" "$GY" 1200 450 20 30 wait 1500 up left wait 80 keyup 56 keyup 125
sleep 2.5
echo "after the drop: p1 $(geo p1) | t1 $(geo t1) | state $(wstate "$ID" | cut -c1-160) | vs:1 tiles $(tilelist Virtual-0/vs:1)"
[ "$(wscreen "$ID")" = "Virtual-0/vs:1" ] || stage_fail "the drop did not leave p1 on vs:1 ($(wscreen "$ID"))"
case "$(tilelist Virtual-0/vs:1)" in *"${P1: -8}"*) ;; *) stage_fail "vs:1 did not take p1 as a tile at the drop" ;; esac
emark
place p1 200 200
sleep 1.5
echo "after the script move into vs:0: p1 $(geo p1)"
elog "Window moved between monitors: .*$P1" | head -2 | sed 's/^/    /'
check "transfer lines for p1 from vs:1 to vs:0" \
    "$(elog "Window moved between monitors: .*$P1" | grep -c "vs:1.*->.*vs:0")" -eq 1
check "vs:1's tiles after the move" "$(tilelist Virtual-0/vs:1)" '!~' "${P1: -8}"
finish
