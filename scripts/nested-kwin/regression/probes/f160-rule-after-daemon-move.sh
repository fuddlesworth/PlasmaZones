#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: GPL-3.0-or-later
#
# f160-rule-after-daemon-move: a window rule scoped to one monitor is applied where a daemon move
# lands, with no focus change. Runner: 2 outputs, flags N.
# Staging: Virtual-0 COLS3, Virtual-1 Columns (2), Snap Assist off. A rule hides p1's title bar on
#   Virtual-1 only. p1 is placed in COLS3 zone 1, then Snap.moveWindowToZone moves it into
#   Columns (2) zone 1 on Virtual-1, and back.
# Asserts: noBorder is false on Virtual-0, true 0.8 s after the landing on Virtual-1 and still
#   1.5 s later, and false again 0.8 s after the move back.
# A FAIL means: the verdict was not owed at the landing, so the title bar stayed until something
#   else re-read the rules.
set -u
NEST="$1"
LOG="$2"
. "$(dirname "$0")/../lib.sh"
kwjs_ok
need_outputs 2
setting snapAssistEnabled false
assign Virtual-0 "$COLS3"
assign Virtual-1 "$COLS2"
sleep 2
R='{"actions":[{"value":true,"type":"setHideTitleBar"}],"enabled":true,"id":"{11111111-2222-4333-8444-555555555571}","match":{"all":[{"field":"title","op":"equals","value":"p1"},{"field":"screenId","op":"equals","value":"Virtual-1"}]},"name":"probe F160","priority":50}'
ADD=$(PZ org.plasmazones.Rules.addRule "$R")
echo "addRule: $ADD"
[ "$ADD" = true ] || stage_fail "the rule was not added ($ADD)"
sleep 1
open p1 300 300
ids
P1U=$(uuid p1)
[ -n "$P1U" ] || stage_fail "p1 never mapped"
P1="org.pz.plain|$P1U"
ZA=$(zoneid "$COLS3" 1)
ZB=$(zoneid "$COLS2" 1)
{ [ -n "$ZA" ] && [ -n "$ZB" ]; } || stage_fail "no zone 1 in COLS3 or Columns (2)"
PZ org.plasmazones.Snap.moveWindowToZone "$P1" "$ZA" >/dev/null
sleep 2
G=$(geo p1)
echo "on Virtual-0: $G"
[ "$(outof "$G")" = Virtual-0 ] || stage_fail "p1 is not on Virtual-0 ($G)"
check "noBorder on Virtual-0" "$(nbof "$G")" = false
PZ org.plasmazones.Snap.moveWindowToZone "$P1" "$ZB" >/dev/null
sleep 0.8
G=$(geo p1)
echo "landed on Virtual-1: $G"
[ "$(outof "$G")" = Virtual-1 ] || stage_fail "p1 did not land on Virtual-1 ($G)"
check "noBorder right after the landing on Virtual-1" "$(nbof "$G")" = true
sleep 1.5
check "noBorder 1.5 s later" "$(nbof "$(geo p1)")" = true
PZ org.plasmazones.Snap.moveWindowToZone "$P1" "$ZA" >/dev/null
sleep 0.8
G=$(geo p1)
echo "back on Virtual-0: $G"
[ "$(outof "$G")" = Virtual-0 ] || stage_fail "p1 did not come back to Virtual-0 ($G)"
check "noBorder right after the move back to Virtual-0" "$(nbof "$G")" = false
finish
