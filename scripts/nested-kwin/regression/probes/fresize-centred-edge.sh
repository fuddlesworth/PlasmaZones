#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: GPL-3.0-or-later
#
# fresize-centred-edge: a user resize of one edge of a scrolling column that the strip keeps centred
# lands that edge where it was released. Runner: 1 output, flags FS.
# Staging: Virtual-0 scrolling, scrollingCenterFocusedColumn 1 (Always). c1 opens as the lone column,
#   centred. KWin's Meta+right resize from the middle of the window's left third takes the left edge
#   only, and drags it 200 px towards the screen's left edge.
# Asserts: after the release and the relayout, c1's left edge is where the drag released it (within a
#   few pixels for KWin's resize increments), and c1 grew by twice the drag.
# A FAIL means: the column took the frame one-sided and the re-centre moved each edge by half the
#   drag, so the dragged edge sprang back from where the user let go.
set -u
NEST="$1"
LOG="$2"
. "$(dirname "$0")/../lib.sh"
kwjs_ok
[ -x "$PZR_FAKEINPUT" ] || stage_fail "no fakeinput binary (the probe needs flag F; run.sh builds it)"
assign Virtual-0 "scrolling:"
gsetting scrollingCenterFocusedColumn "<1>" >/dev/null
sleep 2
open c1 400 300
sleep 2
activate c1
sleep 1
G0=$(geo c1)
read -r X0 Y0 W0 H0 _ <<< "$G0"
X0=${X0%.*}
Y0=${Y0%.*}
W0=${W0%.*}
H0=${H0%.*}
read -r OX OY OW OH <<< "$(scr Virtual-0)"
echo "before: c1 $G0 | output $OX $OY ${OW}x$OH"
CENTRE=$((OX + OW / 2))
[ $((X0 + W0 / 2 - CENTRE)) -ge -4 ] && [ $((X0 + W0 / 2 - CENTRE)) -le 4 ] \
    || stage_fail "c1 is not centred before the resize ($G0)"
DRAG=200
[ $((X0 - DRAG)) -gt "$OX" ] || stage_fail "no room to drag the left edge $DRAG px ($G0)"
PX=$((X0 + W0 / 6))
PY=$((Y0 + H0 / 2))
fake abs "$PX" "$PY" wait 100 keydown 125 wait 50 down right wait 150 \
    path "$PX" "$PY" $((PX - DRAG)) "$PY" 10 30 wait 300 up right wait 50 keyup 125
sleep 2.5
G1=$(geo c1)
read -r X1 _ W1 _ _ <<< "$G1"
X1=${X1%.*}
W1=${W1%.*}
echo "after: c1 $G1"
check "c1's left edge against the release point $((X0 - DRAG))" "$((X1 - (X0 - DRAG)))" -ge -4
check "c1's left edge against the release point $((X0 - DRAG)) (upper)" "$((X1 - (X0 - DRAG)))" -le 4
check "c1's width against twice the drag ($((W0 + 2 * DRAG)))" "$((W1 - (W0 + 2 * DRAG)))" -ge -8
check "c1's width against twice the drag ($((W0 + 2 * DRAG))) (upper)" "$((W1 - (W0 + 2 * DRAG)))" -le 8
finish
