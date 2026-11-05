#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: GPL-3.0-or-later
#
# f762-desktop-move-focus: moving the focused window to another desktop hands focus to the window
# left in view. Runner: 1 output, flags N.
# Staging: two desktops, Virtual-0 COLS3, Snap Assist off. p2 is snapped into zone 1 and p1 into
#   zone 3 (the rightmost) on desktop 1, and p1 is focused. Snap.moveWindowToAdjacentZone right at
#   the last zone carries p1 to desktop 2.
# Asserts: p1 is on desktop 2, desktop 1 stays in view, and p2 is the active window.
# A FAIL means: p1 stays active while hidden on desktop 2.
set -u
NEST="$1"
LOG="$2"
. "$(dirname "$0")/../lib.sh"
kwjs_ok
newdesk 1 Two
setting snapAssistEnabled false
assign Virtual-0 "$COLS3"
sleep 2
for c in p2 p1; do
    open "$c" 300 300
done
ids
P1U=$(uuid p1)
P2U=$(uuid p2)
{ [ -n "$P1U" ] && [ -n "$P2U" ]; } || stage_fail "p1 or p2 never mapped"
Z1=$(zoneid "$COLS3" 1)
Z3=$(zoneid "$COLS3" 3)
{ [ -n "$Z1" ] && [ -n "$Z3" ]; } || stage_fail "COLS3 has no zones 1 and 3"
PZ org.plasmazones.Snap.moveWindowToZone "org.pz.plain|$P2U" "$Z1" >/dev/null
PZ org.plasmazones.Snap.moveWindowToZone "org.pz.plain|$P1U" "$Z3" >/dev/null
sleep 2
activate p1
sleep 0.4
echo "before: active=$(activecap) | p1 $(geo p1) | p2 $(geo p2)"
[ "$(activecap)" = p1 ] || stage_fail "p1 is not the active window"
[ "$(deskof "$(geo p1)")" = 1 ] || stage_fail "p1 is not on desktop 1"
PZ org.plasmazones.Snap.moveWindowToAdjacentZone right >/dev/null
sleep 2
echo "after move right at the last zone: active=$(activecap) | cur $(curdesk) | p1 $(geo p1) | p2 $(geo p2)"
check "p1's desktops after the move" "$(deskof "$(geo p1)")" = 2
check "the desktop in view" "$(curdesk)" = "1 of 2"
check "the active window after the move" "$(activecap)" = p2
finish
