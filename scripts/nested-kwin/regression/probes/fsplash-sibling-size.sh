#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: GPL-3.0-or-later
#
# fsplash-sibling-size: an app's splash screen, below the minimum window size, does not hand its size
# and spot to the app's main window. Runner: 1 output, flag S, PZ_FSPLASH_MODE=scroll or snap.
# Staging: minimum window size 500x500. Virtual-0 scrolling, or COLS3 for snap. Two apps, each with a
#   300x350 splash and a 940x700 main window under one app id. org.pz.early closes its splash before
#   its main window opens (the shape the report describes); org.pz.late opens its main window while
#   its splash is still up and closes the splash after, the control no record reaches.
# Asserts: the early main window is not at the splash's frame. scroll: it is not floating. snap: it
#   opens at the same size as the late main window, the size the client asked for.
# A FAIL means: the splash's close left a floating record in the app's bucket and the main window
#   consumed it as a reopen, floating at the splash's size and spot (and, under the minimum size,
#   out of tiling for good).
set -u
NEST="$1"
LOG="$2"
. "$(dirname "$0")/../lib.sh"
kwjs_ok
MODE="${PZ_FSPLASH_MODE:-scroll}"
gsetting minimumWindowWidth "<500>" >/dev/null
gsetting minimumWindowHeight "<500>" >/dev/null
if [ "$MODE" = snap ]; then
    assign Virtual-0 "$COLS3"
else
    assign Virtual-0 "scrolling:"
fi
sleep 2
dmark
openapp early-splash org.pz.early 300 350
SPLASH=$(geo early-splash)
[ -n "$SPLASH" ] || stage_fail "the early splash never mapped"
echo "early splash: $SPLASH"
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
dlog "takeForReopen: [^ ]*consumed [^ ]*org.pz.(early|late).*|resolveWindowRestore: [^ ]*org.pz.(early|late)[^ ]* -> .*" \
    | sed 's/^/    /'
check "the early main window's frame against the splash's" "$(rect "$EG")" != "$(rect "$SPLASH")"
if [ "$MODE" = snap ]; then
    check "the early main window's size" "$(size "$EG")" = "$(size "$LG")"
else
    check "the early main window floating" "$(floating "$EU")" = false
fi
finish
