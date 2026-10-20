#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: GPL-3.0-or-later
#
# f310-overflow-float-half: an overflow float is counted on the virtual screen it lands on, and
# the per-frame detector does not hand it on. Runner: 1 output, flag S.
# Staging: Virtual-0 split into vs:0 (left half) and vs:1 (right half). a1 and w1 (plain_gtk) are
#   placed free, a1 in the left half and w1 in the right, then both halves go
#   autotile:master-stack, so w1's pre-tile rect is filed in vs:1. move_window_left carries w1 to
#   vs:0, vs:0's per-screen MaxWindows drops to 1 and vs:0 is retiled, so w1 overflows and floats
#   back to its free rect in vs:1. Then vs:0's MaxWindows goes back to 5 (a retile of vs:0) and
#   Meta+F (toggle_window_float, w1 active) tiles w1 again.
# Asserts: the effect logs "Autotile batch float: <w1> floats on Virtual-0/vs:1 where its spot
#   is"; the daemon gets setWindowFloatingForScreen(w1, true, vs:1) and windowCrossedScreens(w1,
#   vs:0 to vs:1); no "Window moved between monitors" line for w1; getWindowState names vs:1;
#   neither half lists w1 as a tile; a1 keeps the whole left half and w1 its free frame, also
#   across the vs:0 retile; Meta+F tiles w1 in the right half.
# A FAIL means: the float stayed counted on vs:0 (its engine kept w1 and Meta+F tiled it beside
#   a1), or the stale notified screen made the effect hand w1 to vs:1's engine as a tile.
set -u
NEST="$1"
LOG="$2"
. "$(dirname "$0")/../lib.sh"
kwjs_ok
read -r _ _ VW VH <<< "$(scr Virtual-0)"
HALF=$((VW / 2))
# maxwindows N: vs:0's per-screen autotile MaxWindows
maxwindows() {
    gdbus call --session --dest org.plasmazones --object-path /PlasmaZones \
        --method org.plasmazones.Settings.setPerScreenSetting Virtual-0/vs:0 autotile MaxWindows "<$1>" 2>&1
}
open a1 400 300
open w1 400 300
place a1 150 150
place w1 1050 150
FREE=$(rect "$(geo w1)")
echo "w1's free frame: $FREE"
case "$FREE" in "1050 150 "*) ;; *) stage_fail "w1 was not placed at 1050,150 ($FREE)" ;; esac
SPLIT='{"screens":[{"index":0,"displayName":"L","region":{"x":0,"y":0,"width":0.5,"height":1}},{"index":1,"displayName":"R","region":{"x":0.5,"y":0,"width":0.5,"height":1}}]}'
echo "split: '$(PZ org.plasmazones.Screen.setVirtualScreenConfig Virtual-0 "$SPLIT")'"
sleep 2
for vs in Virtual-0/vs:0 Virtual-0/vs:1; do
    assign "$vs" "autotile:master-stack"
done
sleep 3
ids
W1=$(uuid w1)
A1=$(uuid a1)
[ -n "$W1" ] && [ -n "$A1" ] || stage_fail "a1 or w1 never mapped"
ID="org.pz.plain|$W1"
# vtiles VS -> the tiles of VS as "name:x:width" (name a1, w1 or an id tail), floats marked float
vtiles() {
    PZ org.plasmazones.Tiling.currentTilesJson "$1" | python3 -c '
import json, sys
names = {sys.argv[1]: "w1", sys.argv[2]: "a1"}
try:
    a = json.load(sys.stdin)
except Exception as e:
    print("unparsable:", e); sys.exit()
print(" ".join(names.get(e.get("windowId", "").split("|")[-1], e.get("windowId", "")[-8:]) + ":" + ("float" if e.get("floating") else "%s:%s" % (e.get("x"), e.get("width"))) for e in a))' "$W1" "$A1"
}
echo "tiled per half: a1 $(geo a1) | w1 $(geo w1)"
read -r AX _ <<< "$(geo a1)"
read -r WX _ <<< "$(geo w1)"
[ "${AX%.*}" -lt "$HALF" ] && [ "${WX%.*}" -ge "$HALF" ] || stage_fail "a1 is not tiled in the left half and w1 in the right"
activate w1
shortcut move_window_left
sleep 2.5
read -r WX _ <<< "$(geo w1)"
echo "after move_window_left: a1 $(geo a1) | w1 $(geo w1)"
[ "${WX%.*}" -lt "$HALF" ] || stage_fail "move_window_left did not carry w1 into the left half"
emark
dmark
echo "MaxWindows 1 on vs:0: '$(maxwindows 1)'"
sleep 1
PZ org.plasmazones.Tiling.retile Virtual-0/vs:0 >/dev/null
sleep 3
OVF=$(elog "Restored pre-autotile geometry for overflow" | head -1)
case "$OVF" in
*"$W1"*) echo "overflowed: w1 $(echo "$OVF" | grep -o 'QRect([^)]*)')" ;;
*"$A1"*) stage_fail "a1 overflowed, not w1 (the crossing did not append w1 last)" ;;
*) stage_fail "no overflow restore line for w1" ;;
esac
elog "Autotile batch float: .*$W1" | head -3 | sed 's/^/    /'
check "batch-float lines for w1 naming its landing on vs:1" \
    "$(elog "Autotile batch float: .*$W1.*floats on" | grep -c "vs:1")" -eq 1
check "vs:1 float sends for w1" \
    "$(dlog "setWindowFloatingForScreen: windowId= [^ ]*$W1" | grep "floating= true" | grep -c 'screen= "Virtual-0/vs:1"')" -ge 1
check "vs:0 to vs:1 crossings for w1" \
    "$(dlog "windowCrossedScreens: [^ ]*$W1" | grep -c 'from "Virtual-0/vs:0" to "Virtual-0/vs:1"')" -ge 1
check "monitor transfer lines for w1 since the overflow" "$(elog "Window moved between monitors: .*$W1" | wc -l)" -eq 0
check "isWindowFloating w1" "$(floating "$ID")" = true
check "getWindowState w1's screen" "$(wscreen "$ID")" = "Virtual-0/vs:1"
check "vs:0 tiles" "$(vtiles Virtual-0/vs:0)" = "a1:0:$HALF"
check "vs:1 tiles naming w1" "$(vtiles Virtual-0/vs:1)" '!~' "w1"
check "a1 after the overflow" "$(rect "$(geo a1)")" = "0 0 $HALF $VH"
check "w1 after the overflow (its free frame)" "$(rect "$(geo w1)")" = "$FREE"
echo "MaxWindows 5 on vs:0: '$(maxwindows 5)'"
sleep 1
PZ org.plasmazones.Tiling.retile Virtual-0/vs:0 >/dev/null
sleep 2.5
check "a1 after the vs:0 retile" "$(rect "$(geo a1)")" = "0 0 $HALF $VH"
check "w1 after the vs:0 retile" "$(rect "$(geo w1)")" = "$FREE"
activate w1
shortcut toggle_window_float
sleep 2.5
check "w1 after Meta+F (tiled in the right half)" "$(rect "$(geo w1)")" = "$HALF 0 $HALF $VH"
check "a1 after Meta+F" "$(rect "$(geo a1)")" = "0 0 $HALF $VH"
check "isWindowFloating w1 after Meta+F" "$(floating "$ID")" = false
echo "  tiles vs:0: $(vtiles Virtual-0/vs:0) | vs:1: $(vtiles Virtual-0/vs:1)"
finish
