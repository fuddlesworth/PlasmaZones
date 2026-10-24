#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: GPL-3.0-or-later
#
# f1154-straddle-peek: a scrolling column that was once wider than it is now peeks past the screen
# edge again once its window takes the park it was given (#1154). Runner: 1 output, flag S.
# Staging: Virtual-0 scrolling, default gaps (8). a is insist_qt.py on Wayland and b a plain GTK
#   window opened after it, so the strip is [a | b], both at half the work area. a is told to hold
#   a frame W, a fifth of the output wider than its column: the column widens to W (the Wayland
#   widening that predates the fix), which leaves the engine a peek memory of W. a then follows
#   its configures again and its column is set to 0.75 of the work area, more than W, so the
#   floor is satisfied and dropped while the memory stays. b is focused: a straddles the left
#   screen edge with less than W showing and parks below the output on the memory. Then
#   kscreen-doctor sets Virtual-0 to scale 1.25 (1280x720 logical), which narrows the work area
#   and with it a's proportional column below W while a still straddles, with no verb and no
#   focus change. The memory stays (a clamp would hand a less than its column) and a parks at
#   the narrower size, which it takes. The gaps cannot narrow it instead: setSetting has no
#   writer for the gap keys, so outerGap is refused as an unknown key.
#   With the default gaps (8), at scale 1 the 0.5 column is about 788 and the 0.75 one about
#   1184, so W is about 1108; at 1.25 the 0.75 column is about 944 and a shows about 636.
# Asserts: a is back on the output, its frame starts at the output's left edge and is narrower
#   than its column (the peek), and the effect reported an answer for a after the scale change.
# The old build (main before #1154): an answer equal to its offer is never reported, a park's
#   included, and the engine has no arm that ends the memory on one, so the memory stands for as
#   long as the column straddles. a stays parked below the output at its whole column: the
#   on-output, width and report checks fail.
set -u
NEST="$1"
LOG="$2"
. "$(dirname "$0")/../lib.sh"
kwjs_ok
read -r OX OY OW OH <<< "$(scr Virtual-0)"
assign Virtual-0 "scrolling:"
sleep 2
CTL="$NEST/f1154-a.ctl"
qtclient insist_qt.py a "$CTL" 0 0
open b 400 300
sleep 1
ids
AU=$(uuid a)
BU=$(uuid b)
[ -n "$AU" ] || stage_fail "a never mapped"
[ -n "$BU" ] || stage_fail "b never mapped"
[ "$(striporder Virtual-0)" = "a | b" ] || stage_fail "the strip is not [a | b] ($(striporder Virtual-0))"

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
# width CAPTION PROPORTION: focus CAPTION and set its column to PROPORTION of the work area
width() {
    activate "$1"
    PZ org.plasmazones.Scrolling.setColumnWidthProportion Virtual-0 "$2" >/dev/null
    sleep 2
}
width b 0.5
width a 0.5
E0=$(extent a)
rd a
FW0=$RW
[ -n "$E0" ] || stage_fail "a is not a column of the strip ($(stripx Virtual-0))"
[ "$(within "$FW0" "$E0" 4)" = yes ] || stage_fail "a did not take its column (frame $FW0, extent $E0)"
[ -r "$CTL.size" ] || stage_fail "a wrote no client size to $CTL.size"
CW0=""
read -r CW0 _ < "$CTL.size"
[ -n "$CW0" ] && [ "$CW0" -gt 0 ] || stage_fail "a's client width reads '$CW0'"
W=$((E0 + OW / 5))
echo "before: strip $(stripx Virtual-0) | a $(geo a) client width $CW0 | b $(geo b)"

echo "== a holds a frame $W wide, wider than its column $E0"
printf '%s 0\n' $((W - (FW0 - CW0))) > "$CTL"
sleep 4
E1=$(extent a)
echo "strip $(stripx Virtual-0) | a $(geo a)"
[ -n "$E1" ] && [ "$(within "$E1" "$W" 4)" = yes ] \
    || stage_fail "a's column did not widen to its held frame $W (extent ${E1:-none}): the engine learned no peek memory"
W=$E1

echo "== a follows its configures again, and its column is set wider than the memory $W"
printf '0 0\n' > "$CTL"
sleep 1
width a 0.75
E2=$(extent a)
echo "strip $(stripx Virtual-0) | a $(geo a)"
[ -n "$E2" ] && [ "$E2" -ge $((W + 20)) ] || stage_fail "a's 0.75 column ${E2:-none} does not offer more than the memory $W"

echo "== b is focused, so a straddles the left edge and parks on the memory"
activate b 2.5
rd a
echo "a $(geo a) | b $(geo b)"
[ "$RY" -ge $((OY + OH)) ] || stage_fail "a did not park on its peek memory while straddling ($(geo a)); the old and new builds both park here"

echo "== scale 1.25 narrows the work area, and with it a's column below the memory"
emark
dmark
on_exit 'ksd output.Virtual-0.scale.1 >/dev/null'
ksd output.Virtual-0.scale.1.25
# The logical size and the daemon's scale follow the change a moment apart; wait for both, then
# for the batch the new work area brings and a's answer to its park.
WANTW=$((OW * 4 / 5))
WANTH=$((OH * 4 / 5))
DSCALE=""
for _ in $(seq 10); do
    sleep 1
    SCR=$(screens)
    read -r OX OY OW OH <<< "$(scr Virtual-0)"
    DSCALE=$(PZ org.plasmazones.Screen.getScreenInfo Virtual-0 | python3 -c 'import json,sys; print(json.load(sys.stdin).get("devicePixelRatio"))' 2>/dev/null)
    [ "$OW" = "$WANTW" ] && [ "$OH" = "$WANTH" ] && [ "$DSCALE" = 1.25 ] && break
done
echo "Virtual-0 $OX,$OY ${OW}x$OH, daemon scale $DSCALE"
[ "$OW" = "$WANTW" ] && [ "$OH" = "$WANTH" ] || stage_fail "Virtual-0 is not ${WANTW}x$WANTH logical at scale 1.25 (${OW}x$OH)"
[ "$DSCALE" = 1.25 ] || stage_fail "the daemon reads scale $DSCALE for Virtual-0, not 1.25"
sleep 5
E3=$(extent a)
rd b
BX=$RX
echo "strip $(stripx Virtual-0) | a $(geo a) | b $(geo b)"
[ -r "$CTL.size" ] && echo "a's client size: $(cat "$CTL.size")"
elog "Reporting committed size for [^ ]*$AU.*|Column answer for [^ ]*$AU.*|Re-seating scroll-managed window [^ ]*$AU.*" \
    | head -8 | sed 's/^/    /'
dlog "reportCommittedSize: [^ ]*$AU.*" | head -4 | sed 's/^/    /'
[ -n "$E3" ] && [ "$E3" -lt $((W - 2)) ] || stage_fail "a's narrowed column ${E3:-none} still offers the memory $W"
# a's column ends the inner gap (8) left of b, and must still reach past the output's left edge
# with more than the 48 px minimum peek showing and less than the memory.
SHOWN=$((BX - 8 - OX))
[ $((BX - 8 - E3)) -lt "$OX" ] || stage_fail "a no longer straddles the left edge (b at $BX, a's column $E3)"
[ "$SHOWN" -ge 48 ] && [ "$SHOWN" -lt "$W" ] || stage_fail "a would show $SHOWN px, outside 48 to $W, so this is not the memory's park"

REPORTS=$(elog "Reporting committed size for [^ ]*$AU" | wc -l)
rd a
check "a is back on the output (y $RY)" "$RY" -lt $((OY + OH))
check "a's frame starts at the output's left edge $OX" "$(within "$RX" "$OX" 2)" = yes
check "a peeks narrower than its column $E3 (width $RW)" "$RW" -lt $((E3 - 2))
check "the effect reported an answer for a after the scale change" "$REPORTS" -ge 1
finish
