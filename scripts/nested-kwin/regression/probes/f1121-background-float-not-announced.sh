#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: GPL-3.0-or-later
#
# f1121-background-float-not-announced: a window that is a tile on the desktop in view and a float
# on another desktop comes back from its monitor's return as a tile, and nothing announces it
# floating. Runner: 2 outputs, no flags.
# Staging: two desktops, Snap Assist off. Virtual-1 (A) autotile:bsp on both desktops, Virtual-0
#   (B) COLS3. On desktop 1 t1 and t2 open on A and tile; t1 is put on desktops 1 and 2, and floated
#   on desktop 2 with the float shortcut (still a tile on desktop 1). With desktop 1 in view A is
#   disabled (verdict 2) and enabled again (verdict 1 Readopt, with an AutotileEngine re-seat line
#   for t1). Then on desktop 1 t1 is activated and the float shortcut pressed.
# Asserts: no windowFloatingChanged(t1, true) signal and no daemon "Window <t1> is now floating"
#   line since the return; the float shortcut on desktop 1 floats t1 and broadcasts
#   windowFloatingChanged(t1, true).
# A FAIL means: the background float seat was announced over the tile in view, and the real float
#   that followed was swallowed as no edge.
set -u
NEST="$1"
LOG="$2"
. "$(dirname "$0")/../lib.sh"
kwjs_ok
need_outputs 2
on_exit 'ksd output.Virtual-1.enable >/dev/null'
setting snapAssistEnabled false
newdesk 1 D2
assignd Virtual-1 1 "autotile:bsp"
assignd Virtual-1 2 "autotile:bsp"
assign Virtual-0 "$COLS3"
sleep 1.5
echo "modes: A 1=$(modeof Virtual-1 1) 2=$(modeof Virtual-1 2) | B 1=$(modeof Virtual-0 1) 2=$(modeof Virtual-0 2) (0 snapping, 1 autotile)"
desk 1
for c in t1 t2; do
    open "$c"
    sendto "$c" Virtual-1 || stage_fail "$c not on Virtual-1 ($(geo "$c"))"
done
sleep 2
ids
T1U=$(uuid t1)
[ -n "$T1U" ] || stage_fail "t1 never mapped"
T1="org.pz.plain|$T1U"
on t1 'w[i].desktops = [workspace.desktops[0], workspace.desktops[1]];'
sleep 2
G=$(geo t1)
echo "staged: t1 (...${T1U: -8}) $G | t2 $(geo t2) | A tiles $(tileorder Virtual-1)"
[ "$(deskof "$G")" = "1,2" ] || stage_fail "t1 is not on desktops 1 and 2 ($G)"
desk 2
activate t1
shortcut toggle_window_float
sleep 2
echo "desktop $(curdesk): t1 $(geo t1) floating=$(floating "$T1")"
[ "$(floating "$T1")" = true ] || stage_fail "t1 is not floating on desktop 2"
desk 1
sleep 1.5
echo "desktop $(curdesk): t1 $(geo t1) floating=$(floating "$T1") | A tiles $(tileorder Virtual-1)"
[ "$(floating "$T1")" = false ] || stage_fail "t1 does not read as a tile on desktop 1"
MON="$NEST/f1121-monitor.log"
mon_start "$MON" "type='signal',interface='org.plasmazones.WindowTracking',member='windowFloatingChanged'"
echo "== Virtual-1 disabled"
dmark
ksd output.Virtual-1.disable
sleep 4
echo "  t1 $(geo t1) | t2 $(geo t2)"
settlerows "$T1U"
[ "$(verdictof "$T1U")" = 2 ] || stage_fail "t1's unplug row is not verdict 2"
echo "== Virtual-1 enabled again, desktop 1 still in view"
dmark
mmark "$MON"
ksd output.Virtual-1.enable
sleep 6
echo "  t1 $(geo t1) | t2 $(geo t2) | desktop $(curdesk)"
settlerows "$T1U"
V=$(verdictof "$T1U")
[ "$V" = 1 ] || stage_fail "t1's return row is not verdict 1 (${V:-no row}); the readopt is not exercised"
[ "$(reseated Autotile "$T1U")" -ge 1 ] || stage_fail "no AutotileEngine::readoptParked re-seat line for t1"
echo "  windowFloatingChanged for t1 since the return, in order: '$(wfc "$MON" "$T1U")'"
check "windowFloatingChanged(t1, true) since the return" "$(wfct "$MON" "$T1U")" -eq 0
check "daemon 'is now floating' lines for t1 since the return" \
    "$(dlog "Window [^ ]*$T1U[^ ]* is now floating" | wc -l)" -eq 0
note "isWindowFloating t1 after the return: $(floating "$T1")"
echo "== desktop 1: t1 activated, float shortcut"
mmark "$MON"
dmark
activate t1
shortcut toggle_window_float
sleep 2
echo "  t1 $(geo t1)"
check "isWindowFloating t1 after the float shortcut" "$(floating "$T1")" = true
check "windowFloatingChanged(t1, true) since the shortcut" "$(wfct "$MON" "$T1U")" -ge 1
finish
