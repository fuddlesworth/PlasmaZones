#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: GPL-3.0-or-later
#
# f1153-cropped-straddler-move: KWin's Move to Screen of a scrolling column cropped at its output's
# right edge hands it to the neighbour's strip under the tracked-output test (#1153). Regression
# row: the old centre test hands this shape over too. The shape #1153 names, a frame centred on
# its own output beside a column centred on the neighbour, does not hold long enough to stage: the
# counter-assert re-seats a cropped straddler centred in its column, which puts the frame's centre
# where the column's is, and KWin then counts the window on that output already. The unit table
# (kwinMoveOffStrip) pins that shape. Runner: 2 outputs, flags S.
# Staging: Virtual-0 and Virtual-1 both scrolling, crop mode on. Virtual-0 holds s1 and s2; s2 is set
#   900 px wide, s1 1100 px and focused, so s2 reaches past Virtual-0's right edge with its centre
#   still on Virtual-0.
#   Step: workspace.sendClientToScreen(s2, Virtual-1).
# Asserts: one daemon "windowCrossedScreens ... from Virtual-0 to Virtual-1" for s2, no "Countering
#   external move" line, s2 on Virtual-1 and a column of Virtual-1's strip, not of Virtual-0's.
# A FAIL means: measured against its tracked output, a cropped straddler's Move to Screen is no
#   longer handed over.
set -u
NEST="$1"
LOG="$2"
. "$(dirname "$0")/../lib.sh"
kwjs_ok
need_outputs 2
gsetting_ok scrollingCropStraddlers "<true>"
assign Virtual-1 "scrolling:"
assign Virtual-0 "scrolling:"
sleep 2
for c in s1 s2; do
    open "$c"
    [ "$(outof "$(geo "$c")")" = Virtual-0 ] || sendto "$c" Virtual-0 || stage_fail "$c not on Virtual-0"
done
activate s2
sleep 1
PZ org.plasmazones.Scrolling.setColumnWidthPixels Virtual-0 900 >/dev/null
sleep 1.5
activate s1
sleep 1
PZ org.plasmazones.Scrolling.setColumnWidthPixels Virtual-0 1100 >/dev/null
sleep 2
ids
read -r V0X _ V0W _ <<< "$(scr Virtual-0)"
read -r x _ w _ <<< "$(geo s2)"
x=${x%.*}
w=${w%.*}
echo "Virtual-0 strip: $(striporder Virtual-0); s1 $(geo s1); s2 $(geo s2)"
[ "$x" -lt $((V0X + V0W)) ] && [ $((x + w)) -gt $((V0X + V0W)) ] \
    || stage_fail "s2 does not straddle Virtual-0's right edge ($(geo s2))"
[ "$(outof "$(geo s2)")" = Virtual-0 ] || stage_fail "KWin counts s2 on another output already ($(geo s2))"
[ "$(instrip s1)" = yes ] && [ "$(instrip s2)" = yes ] \
    || stage_fail "Virtual-0's strip does not hold both s1 and s2 ($(striporder Virtual-0))"
S2=$(uuid s2)
[ -n "$S2" ] || stage_fail "s2 has no id"

echo "== KWin sends s2 to Virtual-1"
emark
dmark
sendto_once s2 Virtual-1
sleep 3
echo "  s2 now: $(geo s2); Virtual-0: $(striporder Virtual-0); Virtual-1: $(striporder Virtual-1)"
check "s2: daemon crossing notices from Virtual-0 to Virtual-1" \
    "$(dlog "windowCrossedScreens: .*$S2" | grep -c "from.*Virtual-0.*to.*Virtual-1")" -eq 1
check "s2: 'Countering external move' lines" \
    "$(elog "Countering external move of scroll-managed window.*$S2" | wc -l)" -eq 0
check "s2: on Virtual-1" "$(outof "$(geo s2)")" = Virtual-1
check "s2: a column of Virtual-1's strip" "$(instrip s2 Virtual-1)" = yes
check "s2: a column of Virtual-0's strip" "$(instrip s2 Virtual-0)" = no
finish
