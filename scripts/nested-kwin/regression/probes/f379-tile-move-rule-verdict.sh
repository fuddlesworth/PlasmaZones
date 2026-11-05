#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: GPL-3.0-or-later
#
# f379-tile-move-rule-verdict: a tiling move to another monitor owes the window its rule verdicts on
# the monitor it lands on, both ways. Runner: 2 outputs, flags S.
# Staging: both outputs autotile:bsp. A rule hides w's title bar on Virtual-1 only. w tiles alone
#   on Virtual-0. Leg 1, same size: move_window_right carries w onto the empty Virtual-1. Then
#   move_window_left takes it back. Leg 2, size change: o1 is tiled on Virtual-1 and
#   move_window_right carries w into half of Virtual-1.
# Asserts: noBorder false on Virtual-0, true 0.8 s after each landing on Virtual-1 and still 1.5 s
#   later, and false again back on Virtual-0.
# A FAIL means: the return leg kept the Virtual-1 verdict, so w stayed without a title bar on
#   Virtual-0.
set -u
NEST="$1"
LOG="$2"
. "$(dirname "$0")/../lib.sh"
kwjs_ok
need_outputs 2
for s in Virtual-0 Virtual-1; do
    assign "$s" "autotile:bsp"
done
sleep 2
R='{"actions":[{"value":true,"type":"setHideTitleBar"}],"enabled":true,"id":"{11111111-2222-4333-8444-555555555581}","match":{"all":[{"field":"title","op":"equals","value":"w"},{"field":"screenId","op":"equals","value":"Virtual-1"}]},"name":"probe F379","priority":50}'
ADD=$(PZ org.plasmazones.Rules.addRule "$R")
echo "addRule: $ADD"
[ "$ADD" = true ] || stage_fail "the rule was not added ($ADD)"
sleep 1
open w
sendto w Virtual-0 || stage_fail "w is not on Virtual-0 ($(geo w))"
G=$(geo w)
echo "w on Virtual-0: $G"
check "noBorder on Virtual-0 at the start" "$(nbof "$G")" = false
activate w 0.8
shortcut move_window_right
sleep 0.8
G=$(geo w)
echo "leg 1, same-size landing on Virtual-1: $G"
[ "$(outof "$G")" = Virtual-1 ] || stage_fail "move_window_right did not carry w onto Virtual-1 ($G)"
check "leg 1: noBorder right after the landing on Virtual-1" "$(nbof "$G")" = true
sleep 1.5
check "leg 1: noBorder 1.5 s later" "$(nbof "$(geo w)")" = true
activate w 0.8
shortcut move_window_left
sleep 2
G=$(geo w)
echo "back on Virtual-0: $G"
[ "$(outof "$G")" = Virtual-0 ] || stage_fail "move_window_left did not take w back to Virtual-0 ($G)"
check "noBorder back on Virtual-0" "$(nbof "$G")" = false
open o1
sendto o1 Virtual-1 || stage_fail "o1 is not on Virtual-1 ($(geo o1))"
echo "o1 tiled on Virtual-1: $(geo o1)"
activate w 0.8
shortcut move_window_right
sleep 0.8
G=$(geo w)
echo "leg 2, landing in half of Virtual-1: $G"
[ "$(outof "$G")" = Virtual-1 ] || stage_fail "move_window_right did not carry w onto Virtual-1 ($G)"
check "leg 2: noBorder right after the landing in half of Virtual-1" "$(nbof "$G")" = true
sleep 1.5
check "leg 2: noBorder 1.5 s later" "$(nbof "$(geo w)")" = true
finish
