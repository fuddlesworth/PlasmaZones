#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: GPL-3.0-or-later
#
# f1130-stale-unsnap-reaches-fallback: a zone notice and its clear, both under an id that is not the
# live window's, reach the same window. Runner: 1 output, no flags.
# Staging: Snap Assist off, Virtual-0 COLS3. A rule on the window titled l: IsSnapped true ->
#   setHideTitleBar, so KWin's noBorder shows the snapped verdict. l and s open (both org.pz.plain),
#   l left free. The nested daemon is frozen (dstop) and Snap.moveWindowToZone(s, zone 1) and
#   Snap.windowUnsnapped(s) are queued behind it with dsend; s's client is killed, so the effect's
#   windowClosed(s) queues behind them. Let run, the daemon emits windowStateChanged(s, snapped)
#   then (s, unsnapped), both reaching the effect after s is gone: the zone notice takes the app-id
#   fallback to l. Counts only when the monitor shows both notices for s and the daemon holds l in
#   no zone. Control (F1111): l snapped for real into zone 2, s2 snapped into zone 3, then
#   windowUnsnapped(s2) queued behind a frozen daemon while s2 closes.
# Asserts: after both notices for s, l's noBorder is false (the clear went to the window the zone
#   notice reached). Control: l keeps noBorder true after the closed s2's clear.
# A FAIL means: the clear resolved s by instance, missed, and left l marked snapped.
set -u
NEST="$1"
LOG="$2"
. "$(dirname "$0")/../lib.sh"
kwjs_ok
dpid >/dev/null || stage_fail "no gated nested daemon pid (the freeze the stale id needs is not available)"
setting snapAssistEnabled false
assign Virtual-0 "$COLS3"
sleep 2
Z1=$(zoneid "$COLS3" 1)
Z2=$(zoneid "$COLS3" 2)
Z3=$(zoneid "$COLS3" 3)
[ -n "$Z1" ] && [ -n "$Z2" ] && [ -n "$Z3" ] || stage_fail "COLS3 zone ids not read ($Z1 $Z2 $Z3)"
R1='{"actions":[{"value":true,"type":"setHideTitleBar"}],"enabled":true,"id":"{11111111-2222-4333-8444-555555551130}","match":{"all":[{"field":"title","op":"equals","value":"l"},{"field":"isSnapped","op":"equals","value":true}]},"name":"probe F1130 snapped","priority":50}'
RA=$(PZ org.plasmazones.Rules.addRule "$R1")
echo "addRule: $RA"
[ "$RA" = true ] || stage_fail "the IsSnapped rule was not added ($RA)"
sleep 1
open l
open s
sleep 1
ids
LU=$(uuid l)
SU=$(uuid s)
[ -n "$LU" ] && [ -n "$SU" ] || stage_fail "l and s did not both map"
L="org.pz.plain|$LU"
S="org.pz.plain|$SU"
GL=$(geo l)
echo "staged: l (...${LU: -8}) $GL zone='$(zoneof "$L")' | s (...${SU: -8}) $(geo s)"
[ "$(nbof "$GL")" = false ] || stage_fail "l already has no border before the act ($GL)"
MON="$NEST/f1130-monitor.log"
mon_start "$MON" "type='signal',interface='org.plasmazones.WindowTracking',member='windowStateChanged'"
mmark "$MON"
dmark
echo "== daemon frozen; snap and unsnap of s queued; s closed"
SPID=$(cpid s) || stage_fail "no client pid for s"
dstop || stage_fail "the daemon could not be frozen"
dsend org.plasmazones.Snap.moveWindowToZone "string:$S" "string:$Z1"
sleep 0.5
dsend org.plasmazones.Snap.windowUnsnapped "string:$S"
sleep 0.5
kill "$SPID"
sleep 1.5
[ "$(wincount s)" = 0 ] || stage_fail "KWin still lists s with the daemon frozen"
dcont
sleep 3
AL=$(geo l)
echo "  l $AL"
dlog "moveWindowToZone: [^ ]*$SU.*|Bus snap of [^ ]*$SU.*|Cleaned up tracking data for closed window [^ ]*$SU.*" \
    | sed 's/org.pz.plain|[^ ]*/<s>/; s/^/    /'
[ "$(dlog "Bus snap of [^ ]*$SU[^ ]* refused" | wc -l)" = 0 ] || stage_fail "the daemon refused the snap of s (it learned of the close first)"
NS=$(msgs "$MON" | grep "^windowStateChanged " | grep "$SU" | grep -c " snapped ")
NU=$(msgs "$MON" | grep "^windowStateChanged " | grep "$SU" | grep -c " unsnapped ")
echo "  windowStateChanged for s: snapped $NS, unsnapped $NU"
{ [ "$NS" -ge 1 ] && [ "$NU" -ge 1 ]; } || stage_fail "the monitor does not show both notices for s"
DZ=$(zoneof "$L")
case "$DZ" in "{"*) stage_fail "the daemon holds l in a zone ($DZ); l would be snapped either way" ;; esac
note "l moved by the stale apply: $(rect "$GL") -> $(rect "$AL")"
check "l's noBorder after the stale notices" "$(nbof "$AL")" = false
echo "== control (F1111): l snapped into zone 2, s2 snapped into zone 3, then s2 unsnapped as it closes"
PZ org.plasmazones.Snap.moveWindowToZone "$L" "$Z2" >/dev/null
sleep 2
GL2=$(geo l)
echo "  l $GL2 zone=$(zoneof "$L")"
[ "$(nbof "$GL2")" = true ] || stage_fail "l snapped into zone 2 does not show the rule's verdict ($GL2)"
open s2
sleep 1
ids
S2U=$(uuid s2)
[ -n "$S2U" ] || stage_fail "s2 never mapped"
S2="org.pz.plain|$S2U"
PZ org.plasmazones.Snap.moveWindowToZone "$S2" "$Z3" >/dev/null
sleep 2
echo "  s2 $(geo s2) zone=$(zoneof "$S2")"
case "$(zoneof "$S2")" in "{"*) ;; *) stage_fail "s2 did not snap into zone 3" ;; esac
S2PID=$(cpid s2) || stage_fail "no client pid for s2"
dstop || stage_fail "the daemon could not be frozen"
dsend org.plasmazones.Snap.windowUnsnapped "string:$S2"
sleep 0.5
kill "$S2PID"
sleep 1.5
[ "$(wincount s2)" = 0 ] || stage_fail "KWin still lists s2 with the daemon frozen"
dcont
sleep 3
check "l's noBorder after the closed s2's clear" "$(nbof "$(geo l)")" = true
finish
