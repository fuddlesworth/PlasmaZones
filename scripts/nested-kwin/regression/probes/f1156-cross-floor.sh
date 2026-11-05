#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: GPL-3.0-or-later
#
# f1156-cross-floor: a window that makes itself taller than its share of a stacked scrolling column
# holds that share, and the tile under it gives way (#1156). Runner: 1 output, flag S.
# Staging: Virtual-0 scrolling. t is insist_qt.py on Wayland (server-side frame, no declared
#   minimum), following its configures, and a, a plain GTK window opened after it; scroll_consume_window
#   from t stacks a under t in one column, each at its share. The column's span S runs from t's top
#   to a's bottom. t is then told to hold a client height whose frame is t's share plus S/6, width
#   free: it commits that height on its own, with no configure asking for it, and again 50 ms after
#   anything sizes it back. Second leg: t holds a frame 4 px short of S, which fits the work area
#   but not the column beside a and the gap between them.
# Asserts: first leg, t stays tiled at its taller height, a starts at or below t's bottom, and a's
#   height is S less t's new frame and the gap (it gave way). Second leg, t floats and a takes the
#   whole span back.
# Not a declared minimum on purpose: a minimum height across the strip already held the stack's
#   split before #1156 (ScrollStrip's minCross), so a client declaring one passes on both builds.
#   The fix is for a client that commits taller than its tile without declaring anything, as the
#   games in the report do.
# The old build (main before #1156): reportCommittedSize floated only an answer taller than the
#   work area and learned nothing from a shorter overshoot, so t hangs over a. In the first leg a
#   keeps its share and its top under t's frame, and in the second t stays tiled over a.
set -u
NEST="$1"
LOG="$2"
. "$(dirname "$0")/../lib.sh"
kwjs_ok
assign Virtual-0 "scrolling:"
sleep 2
CTL="$NEST/f1156-t.ctl"
qtclient insist_qt.py t "$CTL" 0 0
open a 400 300
sleep 1
ids
TU=$(uuid t)
AU=$(uuid a)
[ -n "$TU" ] || stage_fail "t never mapped"
[ -n "$AU" ] || stage_fail "a never mapped"
[ "$(striporder Virtual-0)" = "t | a" ] || stage_fail "the strip is not [t | a] ($(striporder Virtual-0))"
activate t
shortcut scroll_consume_window
sleep 2.5
[ "$(striporder Virtual-0)" = "t+a" ] || stage_fail "consume did not stack a under t ($(striporder Virtual-0))"

# rd CAPTION: CAPTION's frame into RX RY RW RH, whole pixels
rd() {
    local g
    g=$(geo "$1")
    [ -n "$g" ] || stage_fail "no frame for $1"
    read -r RX RY RW RH _ <<< "$g"
    RX=${RX%.*}
    RY=${RY%.*}
    RW=${RW%.*}
    RH=${RH%.*}
}
rd t
TY=$RY
TH=$RH
rd a
AY=$RY
AH=$RH
GAP=$((AY - TY - TH))
SPAN=$((AY + AH - TY))
[ -r "$CTL.size" ] || stage_fail "t wrote no client size to $CTL.size"
read -r _ CH0 < "$CTL.size"
TB=$((TH - CH0))
echo "stacked: t $(geo t) client height $CH0 | a $(geo a) | gap $GAP, span $SPAN"
[ "$GAP" -ge 0 ] && [ "$GAP" -lt 40 ] || stage_fail "t and a are not stacked one above the other (gap $GAP)"
[ "$(within "$TH" "$AH" 4)" = yes ] || stage_fail "t and a do not split the column evenly ($TH vs $AH)"
# The second leg floats t only when its floor of S less 4 outgrows what the column can give it
# beside a, S less the gap and a's minimum, by more than a pixel: so the gap and a's minimum
# together must pass 5. The default inner gap (8) alone does.
[ "$GAP" -ge 6 ] || stage_fail "the gap between t and a is $GAP; the second leg needs the default inner gap"

echo "== t makes itself taller than its share"
FT=$((TH + SPAN / 6))
dmark
printf '0 %s\n' $((FT - TB)) > "$CTL"
sleep 4
rd t
T1Y=$RY
T1H=$RH
rd a
A1Y=$RY
A1H=$RH
echo "t $(geo t) | a $(geo a)"
dlog "reportCommittedSize: [^ ]*$TU.*" | head -3 | sed 's/^/    /'
[ "$(within "$T1H" "$FT" 4)" = yes ] || stage_fail "t did not commit its held frame height $FT ($(geo t)): the client could not resize itself"
check "t is not floating" "$(floating "$TU")" = false
check "t's top stays at the column's top $TY" "$(within "$T1Y" "$TY" 2)" = yes
check "a starts at or below t's bottom $((T1Y + T1H))" "$A1Y" -ge $((T1Y + T1H - 1))
check "a's height gave way to $((SPAN - FT - GAP))" "$(within "$A1H" $((SPAN - FT - GAP)) 4)" = yes

echo "== t makes itself taller than the column can hold beside a"
FT2=$((SPAN - 4))
dmark
printf '0 %s\n' $((FT2 - TB)) > "$CTL"
sleep 4
rd a
A2Y=$RY
A2H=$RH
echo "t $(geo t) | a $(geo a) | strip $(striporder Virtual-0)"
dlog "reportCommittedSize: [^ ]*$TU.*" | head -3 | sed 's/^/    /'
check "t floats once the column cannot hold it beside a" "$(floating "$TU")" = true
check "a's top is back at the column's top $TY" "$(within "$A2Y" "$TY" 2)" = yes
check "a takes the whole span $SPAN back" "$(within "$A2H" "$SPAN" 4)" = yes
finish
