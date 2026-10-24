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
#   the strip and dropped. Only this trigger-held insert is staged. Not covered here: the size the
#   drop indicator shows, the drop that settles through the popup, and the set-column-width arm for
#   a window that floated as it opened and is adopted when dragged in.
# Asserts: f is a column of the strip and not floating after the drop, at the size it was floating
#   at.
# A FAIL means: the drop sized the column from the remembered tile or the configured default
#   rather than from the window, the M/S report where a floating window shrank on entering the
#   strip.
set -u
NEST="$1"
LOG="$2"
. "$(dirname "$0")/../lib.sh"
kwjs_ok
assign Virtual-0 "scrolling:"
gsetting_ok scrollingDefaultColumnWidthKind "<2>"
gsetting_ok scrollingDefaultWindowHeightKind "<3>"
sleep 2
open c1 400 300
open f 500 350
sleep 2
ids
FU=$(uuid f)
[ -n "$FU" ] || stage_fail "f never mapped"
echo "tiled: c1 $(geo c1) | f $(geo f)"
floatkey f
[ "$(floating "$FU")" = true ] || stage_fail "the float shortcut did not float f ($(geo f))"
G0=$(geo f)
read -r X Y W H _ <<< "$G0"
CX=$((${X%.*} + ${W%.*} * 3 / 4))
CY=$((${Y%.*} + ${H%.*} * 3 / 4))
# KWin's modifier resize: Meta+right drag from inside the lower-right quarter, so it takes the
# bottom-right corner and both axes change.
metaresize "$CX" "$CY" 250 150
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
check "f is not floating after the drop" "$(floating "$FU")" = false
check "f is a column of the strip after the drop" "$(instrip f)" = yes
check "f's size after the drop" "$(size "$G2")" = "$(size "$G1")"
finish
