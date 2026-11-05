#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: GPL-3.0-or-later
#
# fresize-then-width-verbs: after a user resize widens a scrolling column, equalize still puts the
# strip back. Runner: 1 output, flags FS.
# Staging: Virtual-0 scrolling, scrollingCenterFocusedColumn 0 (Never), with crop mode on so a
#   straddler is committed at its true rect and its frame runs past the output's edge. c1 and c2
#   open as two half-width columns. KWin's Meta+right resize from the middle of c1's right third
#   drags its right edge 250 px right, which pushes c2 part way past the output's edge. Then
#   scroll_equalize_column_widths. Then the same widen again, and retile (the scrolling reset).
# Asserts: equalize puts c1 back at the even split it opened at and brings c2 back inside the
#   output. c2's own width is not asserted there: the drag never changes it, so it reads the even
#   split on both builds. The reset half is a CONTROL: the reset was never affected, so it passes
#   on both builds and only shows the staging still widens and puts back as expected.
# A FAIL on equalize means: the verb counted only fully visible columns, found c1 alone and did
#   nothing, so c1 kept the hand-dragged width and c2 stayed past the edge.
set -u
NEST="$1"
LOG="$2"
. "$(dirname "$0")/../lib.sh"
kwjs_ok
assign Virtual-0 "scrolling:"
gsetting_ok scrollingCenterFocusedColumn "<0>"
gsetting_ok scrollingCropStraddlers "<true>"
sleep 2
open c1 400 300
sleep 2
open c2 400 300
sleep 2
activate c1
sleep 1

# cframe CAPTION: read the window's frame into X Y W H, whole pixels
cframe() {
    read -r X Y W H _ <<< "$(geo "$1")"
    X=${X%.*}
    Y=${Y%.*}
    W=${W%.*}
    H=${H%.*}
}
show() { echo "$1: c1 $(geo c1) | c2 $(geo c2) | strip $(stripx)"; }

show "before"
cframe c2
W2=$W
cframe c1
[ "$(within "$W" "$W2" 2)" = yes ] || stage_fail "c1 and c2 are not equal before the resize ($(geo c1), $(geo c2))"
EVEN=$W

# widen WHAT: focus c1 and drag its right edge 250 px right, so c2 ends up straddling the edge
widen() {
    activate c1
    sleep 1
    cframe c1
    local w0="$W"
    metaresize $((X + W * 5 / 6)) $((Y + H / 2)) 250 0
    show "after widening c1 ($1)"
    cframe c1
    [ "$W" -gt $((w0 + 100)) ] || stage_fail "the Meta+right drag did not widen c1 ($(geo c1))"
    # Crop mode commits a straddler at its true rect, so its frame runs past the output's edge.
    cframe c2
    [ $((X + W)) -gt "$(scr Virtual-0 | awk '{ print $1 + $3 }')" ] \
        || stage_fail "the widen did not push c2 past the output's edge ($(geo c2))"
}

widen "equalize"
shortcut scroll_equalize_column_widths
sleep 2.5
show "after equalize"
cframe c1
check "c1 is back at the even split after equalize" "$(within "$W" "$EVEN" 8)" = yes
cframe c2
check "c2 ends inside the output after equalize" "$((X + W))" -le "$(scr Virtual-0 | awk '{ print $1 + $3 }')"

widen "reset"
shortcut retile
sleep 2.5
show "after reset"
cframe c1
check "control: c1 is back at the default width after the reset" "$(within "$W" "$EVEN" 8)" = yes
cframe c2
check "control: c2 is at the default width after the reset" "$(within "$W" "$EVEN" 8)" = yes
finish
