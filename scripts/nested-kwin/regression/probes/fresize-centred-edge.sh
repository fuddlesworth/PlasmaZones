#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: GPL-3.0-or-later
#
# fresize-centred-edge: a user resize of one edge of a scrolling column that the strip keeps centred
# lands that edge where it was released, along the strip and across it. Runner: 1 output, flags FS.
# Staging: Virtual-0 scrolling, scrollingCenterFocusedColumn 1 (Always). c1 opens as the lone column,
#   centred. KWin's Meta+right resize from the middle of the window's left third takes the left edge
#   only, and drags it 200 px towards the screen's left edge, then a second time 100 px from where
#   the first left it. Then scrollingCenterShortColumns on, and the same resize from the middle of
#   the bottom third takes the bottom edge only, dragging it 150 px up, and from the middle of the
#   top third takes the top edge only, dragging it 100 px down.
# Asserts: after each release and relayout, the dragged edge is where the drag released it (within a
#   few pixels for KWin's resize increments), and c1's extent on that axis changed by twice the drag:
#   the left drags grow the column, the bottom and top drags shrink it.
# A FAIL means: the column took the frame one-sided and the re-centre moved each edge by half the
#   drag, so the dragged edge sprang back from where the user let go.
set -u
NEST="$1"
LOG="$2"
. "$(dirname "$0")/../lib.sh"
kwjs_ok
assign Virtual-0 "scrolling:"
gsetting_ok scrollingCenterFocusedColumn "<1>"
sleep 2
open c1 400 300
sleep 2
activate c1
sleep 1
read -r OX OY OW OH <<< "$(scr Virtual-0)"

# cframe: read c1's frame into X Y W H, whole pixels
cframe() {
    read -r X Y W H _ <<< "$(geo c1)"
    X=${X%.*}
    Y=${Y%.*}
    W=${W%.*}
    H=${H%.*}
}
cframe
echo "before: c1 $X,$Y ${W}x$H | output $OX $OY ${OW}x$OH"
CENTRE=$((OX + OW / 2))
[ "$(within $((X + W / 2)) "$CENTRE" 4)" = yes ] || stage_fail "c1 is not centred before the resize ($(geo c1))"

# leftdrag DRAG: drag c1's left edge DRAG px left from the middle of its left third and check it
leftdrag() {
    local x0="$X" w0="$W" g0
    g0=$(geo c1)
    [ $((x0 - $1)) -gt "$OX" ] || stage_fail "no room to drag the left edge $1 px ($g0)"
    metaresize $((x0 + w0 / 6)) $((Y + H / 2)) "-$1" 0
    cframe
    echo "after a $1 px left-edge drag: c1 $(geo c1)"
    [ "$X $W" != "$x0 $w0" ] || stage_fail "the Meta+right drag did not resize c1 ($g0)"
    check "c1's left edge against the release point $((x0 - $1))" "$(within "$X" $((x0 - $1)) 4)" = yes
    check "c1's width against twice the $1 px drag ($((w0 + 2 * $1)))" "$(within "$W" $((w0 + 2 * $1)) 8)" = yes
}
leftdrag 200
# A second drag from the frame the first left: the re-centre after it must not undo the first.
leftdrag 100

# Across the strip, with short columns centred.
gsetting_ok scrollingCenterShortColumns "<true>"
sleep 2
cframe
echo "before the bottom-edge drag: c1 $(geo c1)"
UP=150
Y0=$Y
H0=$H
metaresize $((X + W / 2)) $((Y + H * 5 / 6)) 0 "-$UP"
cframe
echo "after the bottom-edge drag: c1 $(geo c1)"
[ "$Y $H" != "$Y0 $H0" ] || stage_fail "the Meta+right drag did not resize c1's bottom edge"
check "c1's bottom edge against the release point $((Y0 + H0 - UP))" "$(within $((Y + H)) $((Y0 + H0 - UP)) 4)" = yes
check "c1's height against twice the drag ($((H0 - 2 * UP)))" "$(within "$H" $((H0 - 2 * UP)) 8)" = yes

DOWN=100
Y0=$Y
H0=$H
metaresize $((X + W / 2)) $((Y + H / 6)) 0 "$DOWN"
cframe
echo "after the top-edge drag: c1 $(geo c1)"
[ "$Y $H" != "$Y0 $H0" ] || stage_fail "the Meta+right drag did not resize c1's top edge"
check "c1's top edge against the release point $((Y0 + DOWN))" "$(within "$Y" $((Y0 + DOWN)) 4)" = yes
check "c1's height against twice the drag ($((H0 - 2 * DOWN)))" "$(within "$H" $((H0 - 2 * DOWN)) 8)" = yes
finish
