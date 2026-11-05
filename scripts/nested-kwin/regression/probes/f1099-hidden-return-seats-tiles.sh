#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: GPL-3.0-or-later
#
# f1099-hidden-return-seats-tiles: autotile windows whose desktop is out of view when their monitor
# returns go back into their tiles, and a parked float floats. Runner: 2 outputs, no flags.
# Staging: two desktops, Snap Assist off. Virtual-1 (A) autotile:bsp on both desktops, Virtual-0
#   (B) COLS3. On desktop 2 t1, t2, t3 open on A and tile; t2 is floated with the float shortcut.
#   Desktop 1 is shown and k opens on B and is activated (the focus witness). A is disabled (KWin
#   moves t1..t3 to B, verdict 2), then enabled with desktop 1 still in view (KWin returns them
#   untouched, verdict 1 Readopt), then desktop 2 is shown.
# Asserts: a parkDropped for each of t1..t3 at the return; k still active after the return; after
#   the switch a Tiling.windowsTileRequested batch names t1 and t3, t2 is floating at its float
#   frame.
# A FAIL means: the out-of-view grant kept the windows parked, so desktop 2 came into view with no
#   batch for them and t2 not floating.
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
echo "modes: A 1=$(modeof Virtual-1 1) 2=$(modeof Virtual-1 2) | B 1=$(modeof Virtual-0 1) 2=$(modeof Virtual-0 2)"
desk 2
echo "desktop $(curdesk)"
for c in t1 t2 t3; do
    open "$c"
    sendto "$c" Virtual-1 || stage_fail "$c not on Virtual-1 ($(geo "$c"))"
done
sleep 2
ids
T1U=$(uuid t1)
T2U=$(uuid t2)
T3U=$(uuid t3)
[ -n "$T1U" ] && [ -n "$T2U" ] && [ -n "$T3U" ] || stage_fail "t1..t3 did not all map"
T2="org.pz.plain|$T2U"
activate t2
shortcut toggle_window_float
sleep 2
G2=$(geo t2)
echo "staged: t1 $(geo t1) | t2 $G2 floating=$(floating "$T2") | t3 $(geo t3) | A tiles $(tileorder Virtual-1)"
[ "$(floating "$T2")" = true ] || stage_fail "t2 is not floating"
desk 1
open k
sendto k Virtual-0 || stage_fail "k not on Virtual-0 ($(geo k))"
activate k
echo "desktop $(curdesk) shown, active: $(activecap)"
[ "$(activecap)" = k ] || stage_fail "k is not the active window before the unplug"
MON="$NEST/f1099-monitor.log"
mon_start "$MON" "type='signal',interface='org.plasmazones.Tiling',member='windowsTileRequested'" \
    "type='signal',interface='org.plasmazones.WindowTracking',member='parkDropped'"
echo "== Virtual-1 disabled"
dmark
ksd output.Virtual-1.disable
sleep 4
echo "  t1 $(geo t1) | t2 $(geo t2) | t3 $(geo t3)"
for u in "$T1U" "$T2U" "$T3U"; do
    settlerows "$u"
    [ "$(verdictof "$u")" = 2 ] || stage_fail "a settle row at the unplug is not verdict 2 (...${u: -8})"
done
echo "== Virtual-1 enabled again, desktop 1 still in view"
dmark
mmark "$MON"
ksd output.Virtual-1.enable
sleep 6
echo "  outputs: $(screens)"
echo "  t1 $(geo t1) | t2 $(geo t2) | t3 $(geo t3)"
for u in "$T1U" "$T2U" "$T3U"; do
    settlerows "$u"
    [ "$(verdictof "$u")" = 1 ] || stage_fail "a return row is not verdict 1 Readopt (...${u: -8}); the readopt arm is not exercised"
done
note "readoptParked re-seat lines: t1 $(reseated Autotile "$T1U") t2 $(reseated Autotile "$T2U") t3 $(reseated Autotile "$T3U")"
check "parkDropped for t1 at the return" "$(named "$MON" parkDropped "$T1U")" -eq 1
check "parkDropped for t2 at the return" "$(named "$MON" parkDropped "$T2U")" -eq 1
check "parkDropped for t3 at the return" "$(named "$MON" parkDropped "$T3U")" -eq 1
check "active window after the return" "$(activecap)" = k
echo "== desktop 2 shown"
mmark "$MON"
desk 2
sleep 2.5
A2=$(geo t2)
echo "  desktop $(curdesk): t1 $(geo t1) | t2 $A2 | t3 $(geo t3)"
check "windowsTileRequested batches naming t1 since the switch" "$(named "$MON" windowsTileRequested "$T1U")" -ge 1
check "windowsTileRequested batches naming t3 since the switch" "$(named "$MON" windowsTileRequested "$T3U")" -ge 1
check "isWindowFloating t2" "$(floating "$T2")" = true
check "t2 at its float frame" "$(rect "$A2")" = "$(rect "$G2")"
note "tiles on Virtual-1 in batch order: $(tileorder Virtual-1)"
finish
