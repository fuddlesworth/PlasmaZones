#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: GPL-3.0-or-later
#
# f1018-carry-not-user-snap: a desktop carry is a re-statement, never a user snap, and a user snap
# into the zone the window holds is one. Runner: 1 output, no flags.
# Staging: two desktops, Virtual-0 COLS3 on both, moveNewWindowsToLastZone true, Snap Assist off.
#   On desktop 1 p1 is snapped into zone 1 and then p2 into zone 2 with the snap_to_zone_N
#   shortcuts, so the last-used zone is zone 2.
#   (a) Desktop 2 shown, p1 moved onto it (KWin's Move to Desktop: the carry lands it in zone 1).
#   (b) p3 user-snapped into zone 3, then snap_to_zone_1 on p1, the zone it already holds.
# Asserts: after (a) WindowTracking.getLastUsedZoneId is still zone 2 and p1 holds zone 1 on
#   desktop 2; after (b) it is zone 1.
# A FAIL means (a): the carry was committed as a user snap and moved the last-used zone.
set -u
NEST="$1"
LOG="$2"
. "$(dirname "$0")/../lib.sh"
kwjs_ok
on_exit 'setting moveNewWindowsToLastZone false'
setting snapAssistEnabled false
setting moveNewWindowsToLastZone true
newdesk 1 D2
assign Virtual-0 "$COLS3"
sleep 1.5
desk 1
Z1=$(zoneid "$COLS3" 1)
Z2=$(zoneid "$COLS3" 2)
Z3=$(zoneid "$COLS3" 3)
[ -n "$Z1" ] && [ -n "$Z2" ] && [ -n "$Z3" ] || stage_fail "COLS3's zone ids could not be read"
name() { case "$1" in "$Z1") echo "zone1" ;; "$Z2") echo "zone2" ;; "$Z3") echo "zone3" ;; "") echo "none" ;; *) echo "$1" ;; esac; }
last() { name "$(PZ org.plasmazones.WindowTracking.getLastUsedZoneId)"; }
open p1
activate p1
shortcut snap_to_zone_1
sleep 1.5
open p2
activate p2
shortcut snap_to_zone_2
sleep 1.5
ids
P1="org.pz.plain|$(uuid p1)"
P2="org.pz.plain|$(uuid p2)"
echo "staged on desktop $(curdesk): p1 $(name "$(zoneof "$P1")") | p2 $(name "$(zoneof "$P2")") | last used $(last)"
[ "$(name "$(zoneof "$P1")")" = zone1 ] || stage_fail "p1 is not in zone 1"
[ "$(last)" = zone2 ] || stage_fail "the last-used zone is not zone 2 after the user snaps"
echo "== (a) desktop 2 shown, p1 moved onto it"
desk 2
todesk p1 2
sleep 2.5
echo "  p1 $(geo p1)"
check "(a) p1's zone after the carry onto desktop 2" "$(name "$(zoneof "$P1")")" = zone1
check "(a) last-used zone after the carry" "$(last)" = zone2
echo "== (b) a user snap into the zone the window holds"
open p3
activate p3
shortcut snap_to_zone_3
sleep 1.5
[ "$(last)" = zone3 ] || stage_fail "the last-used zone is not zone 3 after p3's user snap"
activate p1
shortcut snap_to_zone_1
sleep 1.5
check "(b) p1's zone" "$(name "$(zoneof "$P1")")" = zone1
check "(b) last-used zone after a user snap into the held zone" "$(last)" = zone1
finish
