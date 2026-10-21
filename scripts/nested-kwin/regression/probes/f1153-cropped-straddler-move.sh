#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: GPL-3.0-or-later
#
# f1153-cropped-straddler-move: KWin's Move to Screen of a scrolling column cropped at its output's
# right edge, the centre of its column rect on the neighbouring output, hands it to that output's
# strip (#1153). Runner: 2 outputs, flags S.
# Staging: Virtual-0 and Virtual-1 both scrolling, crop mode on. Virtual-0 holds s1 and s2; s2 is set
#   1500 px wide, s1 1000 px and focused, so s2 reaches past Virtual-0's right edge with its centre
#   on Virtual-1.
#   Step: workspace.sendClientToScreen(s2, Virtual-1).
# Asserts: one daemon "windowCrossedScreens ... from Virtual-0 to Virtual-1" for s2, no "Countering
#   external move" line, s2 on Virtual-1 and a column of Virtual-1's strip, not of Virtual-0's.
# A FAIL means: the output-change arm measured the strip's output at the centre of s2's column rect,
#   found it on Virtual-1 already, and never handed the window over.
set -u
NEST="$1"
LOG="$2"
. "$(dirname "$0")/../lib.sh"
kwjs_ok
need_outputs 2
send1() { js "var o = null; for (var i = 0; i < workspace.screens.length; i++) { if (workspace.screens[i].name === \"$2\") { o = workspace.screens[i]; } } const w = workspace.windowList(); for (var j = 0; j < w.length; j++) { if (w[j].caption === \"$1\" && o && w[j].output !== o) { workspace.sendClientToScreen(w[j], o); } }"; }
setting scrollingCropStraddlers true
assign Virtual-1 "scrolling:"
assign Virtual-0 "scrolling:"
sleep 2
for c in s1 s2; do
    open "$c"
    [ "$(outof "$(geo "$c")")" = Virtual-0 ] || sendto "$c" Virtual-0 || stage_fail "$c not on Virtual-0"
done
activate s2
sleep 1
PZ org.plasmazones.Scrolling.setColumnWidthPixels Virtual-0 1500 >/dev/null
sleep 1.5
activate s1
sleep 1
PZ org.plasmazones.Scrolling.setColumnWidthPixels Virtual-0 1000 >/dev/null
sleep 2
ids
read -r V0X _ V0W _ <<< "$(scr Virtual-0)"
read -r x _ w _ <<< "$(geo s2)"
x=${x%.*}
w=${w%.*}
echo "Virtual-0 strip: $(striporder Virtual-0); s1 $(geo s1); s2 $(geo s2)"
[ "$x" -lt $((V0X + V0W)) ] && [ $((x + w / 2)) -gt $((V0X + V0W)) ] \
    || stage_fail "s2 does not straddle Virtual-0's right edge with its centre past it ($(geo s2))"
S2=$(uuid s2)
[ -n "$S2" ] || stage_fail "s2 has no id"

echo "== KWin sends s2 to Virtual-1"
emark
dmark
send1 s2 Virtual-1
sleep 3
echo "  s2 now: $(geo s2); Virtual-0: $(striporder Virtual-0); Virtual-1: $(striporder Virtual-1)"
check "s2: daemon crossing notices from Virtual-0 to Virtual-1" \
    "$(dlog "windowCrossedScreens: .*$S2" | grep -c "from.*Virtual-0.*to.*Virtual-1")" -eq 1
check "s2: 'Countering external move' lines" \
    "$(elog "Countering external move of scroll-managed window.*$S2" | wc -l)" -eq 0
check "s2: on Virtual-1" "$(outof "$(geo s2)")" = Virtual-1
check "s2: a column of Virtual-1's strip" "$(striporder Virtual-1)" '~' "s2"
check "s2: gone from Virtual-0's strip" "$(striporder Virtual-0)" '!~' "s2"
finish
