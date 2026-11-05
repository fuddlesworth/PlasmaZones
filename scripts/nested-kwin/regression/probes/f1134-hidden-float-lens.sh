#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: GPL-3.0-or-later
#
# f1134-hidden-float-lens: a snap float stated on a hidden desktop assigned tiling while tiling is
# switched off reads floating while that desktop is still hidden. Runner: 2 outputs, no flags.
# Staging: two desktops, per-output desktops off, Snap Assist off, autotileEnabled false (set before
#   the assignment). Virtual-1 (A) COLS3 on both desktops, Virtual-0 (B) desktop 1 COLS3 and desktop 2
#   autotile:bsp (raw mode autotile, running mode snapping). On desktop 2 w opens on A, is snapped into
#   zone 1 and floated with the float shortcut. Desktop 1 is shown, then A is disabled and left off:
#   KWin moves w to B on hidden desktop 2 and the settle states the snap float into B desktop 2's snap
#   context (handoffReceive wasFloating= true, F1122).
# Asserts: with desktop 2 still hidden, isWindowFloating(w) is true and getFloatingWindows names w;
#   once desktop 2 is shown w still reads floating.
# A FAIL means: the out-of-view float lens followed the raw autotile assignment to the autotile slot,
#   which holds nothing for w.
set -u
NEST="$1"
LOG="$2"
. "$(dirname "$0")/../lib.sh"
kwjs_ok
need_outputs 2
on_exit 'ksd output.Virtual-1.enable >/dev/null; setting autotileEnabled true'
setting snapAssistEnabled false
newdesk 1 D2
setting autotileEnabled false
sleep 1
echo "autotileEnabled reads $(PZ org.plasmazones.Settings.getSetting autotileEnabled)"
assignd Virtual-1 1 "$COLS3"
assignd Virtual-1 2 "$COLS3"
assignd Virtual-0 1 "$COLS3"
assignd Virtual-0 2 "autotile:bsp"
sleep 1.5
echo "modes: A 1=$(modeof Virtual-1 1) 2=$(modeof Virtual-1 2) | B 1=$(modeof Virtual-0 1) 2=$(modeof Virtual-0 2) (0 snapping, 1 autotile)"
[ "$(modeof Virtual-0 2)" = 1 ] || stage_fail "B desktop 2 does not read the autotile mode ($(modeof Virtual-0 2))"
desk 2
open w
sendto w Virtual-1 || stage_fail "w not on Virtual-1 ($(geo w))"
sleep 1.5
ids
WU=$(uuid w)
[ -n "$WU" ] || stage_fail "w never mapped"
W="org.pz.plain|$WU"
PZ org.plasmazones.Control.snapWindowToZone "$W" 1 Virtual-1 >/dev/null
sleep 2
activate w
shortcut toggle_window_float
sleep 2
echo "staged: w (...${WU: -8}) $(geo w) floating=$(floating "$W") zone='$(zoneof "$W")'"
[ "$(floating "$W")" = true ] || stage_fail "w is not snap-floating on Virtual-1"
desk 1
echo "desktop $(curdesk) shown"
echo "== Virtual-1 disabled and left off; desktop 2 stays hidden"
dmark
ksd output.Virtual-1.disable
sleep 4
G=$(geo w)
echo "  w $G | desktop $(curdesk)"
[ "$(outof "$G")" = Virtual-0 ] || stage_fail "w was not evacuated to Virtual-0 ($G)"
[ "$(deskof "$G")" = 2 ] || stage_fail "w is not on desktop 2 alone ($G); the hidden-desktop arm is not exercised"
settlerows "$WU"
[ "$(verdictof "$WU")" = 2 ] || stage_fail "w's settle row is not verdict 2 ($(verdictof "$WU"))"
H=$(handoffs "$WU")
[ -n "$H" ] && echo "$H" | sed 's/^/    /'
check "snap handoffReceive stating w floating at the settle" "$(echo -n "$H" | grep -c "wasFloating= true")" -eq 1
check "isWindowFloating(w) with desktop 2 hidden" "$(floating "$W")" = true
check "getFloatingWindows names w with desktop 2 hidden" \
    "$(yesno 'PZ org.plasmazones.WindowTracking.getFloatingWindows | grep -q "$WU"')" = yes
echo "== desktop 2 shown"
desk 2
sleep 2
echo "  desktop $(curdesk): w $(geo w) zone='$(zoneof "$W")'"
check "isWindowFloating(w) with desktop 2 in view" "$(floating "$W")" = true
finish
