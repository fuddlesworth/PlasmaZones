#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: GPL-3.0-or-later
#
# f653-cross-screen-leg-rule: a daemon move onto another monitor takes the animation timing a rule
# gives the window on the monitor it lands on. Runner: 2 outputs, no flags.
# Staging: Virtual-0 COLS3, Virtual-1 Columns (2), Snap Assist off. A rule gives f1 a 1900 ms placeIn
#   leg on Virtual-1 only. f1 is moved by Snap.moveWindowToZone into COLS3 zone 1, then into
#   Columns (2) zone 1 on Virtual-1, then (control) into Columns (2) zone 2.
# Asserts: the leg into Virtual-0 runs at another duration than 1900 ms (the rule does not match
#   there), the cross-screen leg onto Virtual-1 runs 1900 ms, and the control leg within Virtual-1
#   runs 1900 ms.
# A FAIL means: the cross-screen leg matched the rule on the monitor it left.
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
R='{"actions":[{"event":"window.movement.placeIn","durationMs":1900,"type":"overrideAnimationTiming"}],"enabled":true,"id":"{11111111-2222-4333-8444-555555555561}","match":{"all":[{"field":"title","op":"equals","value":"f1"},{"field":"screenId","op":"equals","value":"Virtual-1"}]},"name":"probe F653","priority":50}'
echo "addRule: $(PZ org.plasmazones.Rules.addRule "$R")"
sleep 1
open f1 300 300
ids
F1U=$(uuid f1)
[ -n "$F1U" ] || stage_fail "f1 never mapped"
F1="org.pz.plain|$F1U"
emark
PZ org.plasmazones.Snap.moveWindowToZone "$F1" "$(zoneid "$COLS3" 1)" >/dev/null
sleep 2.5
G=$(geo f1)
echo "f1 on Virtual-0: $G"
[ "$(outof "$G")" = Virtual-0 ] && [ "$(zoneof "$F1")" = "$(zoneid "$COLS3" 1)" ] \
    || stage_fail "f1 did not land in COLS3 zone 1 ($G)"
L=$(lastlegms)
[ -n "$L" ] || stage_fail "no animation leg into COLS3 zone 1"
check "leg into COLS3 zone 1 on Virtual-0 (no rule there)" "$L" != 1900
emark
PZ org.plasmazones.Snap.moveWindowToZone "$F1" "$(zoneid "$COLS2" 1)" >/dev/null
sleep 3
G=$(geo f1)
echo "f1 on Virtual-1: $G"
[ "$(outof "$G")" = Virtual-1 ] || stage_fail "f1 did not reach Virtual-1 ($G)"
check "cross-screen leg onto Virtual-1" "$(lastlegms)" = 1900
emark
PZ org.plasmazones.Snap.moveWindowToZone "$F1" "$(zoneid "$COLS2" 2)" >/dev/null
sleep 3
check "control leg within Virtual-1" "$(lastlegms)" = 1900
finish
