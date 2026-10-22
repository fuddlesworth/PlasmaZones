#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: GPL-3.0-or-later
#
# fdrag-float-keeps-size: a floating window dragged into the scrolling strip with the column width
# and window height both set to "Window decides" keeps the size it had. Runner: 1 output, flags FS.
# Staging: Virtual-0 scrolling, scrollingDefaultColumnWidthKind 2 and scrollingDefaultWindowHeightKind
#   3 (both "Window decides"). c1 and f open as columns, f is floated with the float shortcut and
#   then resized by a Meta+right drag, so the tile shape it was floated out of no longer matches the
#   size on screen. f is then Meta-dragged with Alt (the scrolling insert trigger) past the end of
#   the strip and dropped.
# Asserts: f is tiled after the drop, at the size it was floating at.
# A FAIL means: the drop sized the column from the remembered tile or the configured default
#   rather than from the window, the M/S report where a floating window shrank on entering the
#   strip.
set -u
NEST="$1"
LOG="$2"
. "$(dirname "$0")/../lib.sh"
kwjs_ok
[ -x "$PZR_FAKEINPUT" ] || stage_fail "no fakeinput binary (the probe needs flag F; run.sh builds it)"
assign Virtual-0 "scrolling:"
gsetting scrollingDefaultColumnWidthKind "<2>" >/dev/null
gsetting scrollingDefaultWindowHeightKind "<3>" >/dev/null
sleep 2
open c1 400 300
open f 500 350
sleep 2
ids
FU=$(uuid f)
[ -n "$FU" ] || stage_fail "f never mapped"
echo "tiled: c1 $(geo c1) | f $(geo f)"
activate f
shortcut toggle_window_float
sleep 2
[ "$(floating "$FU")" = true ] || stage_fail "the float shortcut did not float f ($(geo f))"
G0=$(geo f)
read -r X Y W H _ <<< "$G0"
CX=$((${X%.*} + ${W%.*} * 3 / 4))
CY=$((${Y%.*} + ${H%.*} * 3 / 4))
# KWin's modifier resize: Meta+right drag from inside the lower-right quarter, so it takes the
# bottom-right corner and both axes change.
fake abs "$CX" "$CY" wait 100 keydown 125 wait 50 down right wait 150 \
    path "$CX" "$CY" $((CX + 250)) $((CY + 150)) 10 30 wait 300 up right wait 50 keyup 125
sleep 2
G1=$(geo f)
echo "floating: before resize $G0 | after resize $G1"
read -r _ _ W1 H1 _ <<< "$G1"
[ "${W1%.*}" != "${W%.*}" ] && [ "${H1%.*}" != "${H%.*}" ] \
    || stage_fail "the Meta+right drag did not resize f on both axes ($G1)"
[ "$(floating "$FU")" = true ] || stage_fail "f stopped floating during the resize"
read -r OX OY OW OH <<< "$(scr Virtual-0)"
dmark
altdrag f $((OX + OW - 60)) $((OY + OH / 2))
sleep 1
G2=$(geo f)
echo "after the drop: f $G2 | c1 $(geo c1)"
dlog "beginDrag: [^ ]*$FU.*|endDrag: [^ ]*$FU.*|drag-insert tick: [^ ]*$FU.*" | sed 's/^/    /'
check "f floating after the drop" "$(floating "$FU")" = false
check "f's size after the drop" "$(size "$G2")" = "$(size "$G1")"
finish
