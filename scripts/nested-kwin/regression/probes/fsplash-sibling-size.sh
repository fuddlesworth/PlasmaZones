#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: GPL-3.0-or-later
#
# fsplash-sibling-size: an app's splash screen, below the minimum window size, does not hand its size
# and spot to the app's main window. Runner: 1 output, flag S, PZ_FSPLASH_MODE=scroll, tile or snap.
# Staging: minimum window size 500x500. Virtual-0 scrolling, bsp for tile, or COLS3 for snap. Two
#   apps, each with a 300x350 splash and a 940x700 main window under one app id. org.pz.early closes
#   its splash before its main window opens (the shape the report describes); org.pz.late opens its
#   main window while its splash is still up and closes the splash after, the control no record
#   reaches. Before the early splash closes, the probe confirms it was kept out of placement: no
#   column or tile holds it (scroll, tile), or the daemon logged it excluded by size (snap).
# Asserts: the early main window is not at the splash's frame. scroll and tile: both main windows
#   are a column or a tile and not floating. snap: the late main window opens at the width it asked
#   for, and the early one at the same size as the late one.
# A FAIL means: the splash's close left a floating record in the app's bucket and the main window
#   consumed it as a reopen, floating at the splash's size and spot (and, under the minimum size,
#   out of tiling for good).
set -u
NEST="$1"
LOG="$2"
. "$(dirname "$0")/../lib.sh"
kwjs_ok
MODE="${PZ_FSPLASH_MODE:-scroll}"
case "$MODE" in
scroll) assign Virtual-0 "scrolling:" ;;
tile) assign Virtual-0 "autotile:bsp" ;;
snap) assign Virtual-0 "$COLS3" ;;
*) stage_fail "unknown PZ_FSPLASH_MODE '$MODE'" ;;
esac
gsetting_ok minimumWindowWidth "<500>"
gsetting_ok minimumWindowHeight "<500>"
sleep 2
# placed CAPTION -> "yes" when the mode's engine holds CAPTION as a column or a tile, else "no"
placed() {
    if [ "$MODE" = tile ]; then
        if tileorder Virtual-0 | tr ' ' '\n' | grep -qxF "$1"; then echo yes; else echo no; fi
    else
        instrip "$1"
    fi
}
dmark
openapp early-splash org.pz.early 300 350
SPLASH=$(geo early-splash)
[ -n "$SPLASH" ] || stage_fail "the early splash never mapped"
ids
SU=$(uuid early-splash)
[ -n "$SU" ] || stage_fail "the early splash has no id"
echo "early splash: $SPLASH"
if [ "$MODE" = snap ]; then
    [ "$(dlog "resolveWindowRestore: [^ ]*$SU[^ ]* excluded by rule or size" | wc -l)" -ge 1 ] \
        || stage_fail "the daemon did not exclude the early splash by size"
else
    [ "$(placed early-splash)" = no ] || stage_fail "the $MODE engine placed the early splash, so it was not excluded by size"
fi
ckill early-splash || stage_fail "no test client pid for the early splash"
sleep 2
openapp early-main org.pz.early 940 700
sleep 2
openapp late-splash org.pz.late 300 350
openapp late-main org.pz.late 940 700
ckill late-splash || stage_fail "no test client pid for the late splash"
sleep 3
ids
EU=$(uuid early-main)
LU=$(uuid late-main)
[ -n "$EU" ] && [ -n "$LU" ] || stage_fail "a main window never mapped"
EG=$(geo early-main)
LG=$(geo late-main)
echo "early-main: $EG floating=$(floating "$EU")"
echo "late-main: $LG floating=$(floating "$LU")"
dlog "takeForReopen: [^ ]* consumed [^ ]*org.pz.(early|late).*|resolveWindowRestore: [^ ]*org.pz.(early|late).*" \
    | sed 's/^/    /'
check "the early main window's frame against the splash's" "$(rect "$EG")" != "$(rect "$SPLASH")"
if [ "$MODE" = snap ]; then
    read -r _ _ LW LH _ <<< "$LG"
    check "the late main window's width, the one it asked for (control)" "${LW%.*}" -eq 940
    check "the late main window's height, at least the one it asked for (control)" "${LH%.*}" -ge 700
    check "the early main window's size" "$(size "$EG")" = "$(size "$LG")"
else
    check "the late main window floating (control)" "$(floating "$LU")" = false
    check "the late main window placed by the $MODE engine (control)" "$(placed late-main)" = yes
    check "the early main window floating" "$(floating "$EU")" = false
    check "the early main window placed by the $MODE engine" "$(placed early-main)" = yes
fi
finish
