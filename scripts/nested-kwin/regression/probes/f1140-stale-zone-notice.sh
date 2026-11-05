#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: GPL-3.0-or-later
#
# f1140-stale-zone-notice: a stale id's zone notice reaching a window whose own id already filed its
# zone neither clears nor overwrites that window's zone. Runner: 1 output, no flags;
#   PZ_F1140_RUN=a (row f1140-stale-zone-notice-clear, F1140) or d (row
#   f1140-stale-zone-notice-write, F1152).
# Staging: Snap Assist off, Virtual-0 COLS3. l and s open (both org.pz.plain) and l is snapped into
#   zone 1 under its own id. The stale id is s's: s is focused, the nested daemon is frozen (dstop),
#   calls naming s are queued behind it (dsend), s's client is killed so the effect's
#   windowClosed(s) queues after them, and the daemon is let run. The notices for s reach the effect
#   after s is gone and take the app-id fallback to l.
#   a: a rule on the window titled l, IsSnapped true -> hide the title bar (KWin's noBorder). Queued:
#     moveWindowToZone(s, zone 2), then Snap.restoreWindowSize(), the restore verb, which acts on
#     the focused window s. Snap.windowUnsnapped is the compositor's own report and refuses this
#     shell, so the restore is the unsnap: it takes s out of zone 2 (an unsnapped notice), floats
#     it (a floated notice) and moves it to its float-back.
#   d: one rule per zone on l, keyed on the Zone field: zone 1 -> keep above, zone 2 -> hide the title
#     bar. Queued: moveWindowToZone(s, zone 2) alone.
# Asserts: a: l keeps noBorder true (s's clear reaches no window) and the daemon still holds l in zone
#   1. d: l keeps the zone-1 rule (keepAbove true, noBorder false), the effect logs "zone notice under
#   <s> not filed on <l> - its own id holds its zone", and the daemon still holds l in zone 1.
# A FAIL means: a: s's clear took l's zone and snapped mark; d: s's zone was written onto l.
set -u
NEST="$1"
LOG="$2"
RUN="${PZ_F1140_RUN:-a}"
. "$(dirname "$0")/../lib.sh"
kwjs_ok
case "$RUN" in a | d) ;; *) stage_fail "unknown PZ_F1140_RUN '$RUN'" ;; esac
dpid >/dev/null || stage_fail "no gated nested daemon pid (the freeze the stale id needs is not available)"
setting snapAssistEnabled false
assign Virtual-0 "$COLS3"
sleep 2
Z1=$(zoneid "$COLS3" 1)
Z2=$(zoneid "$COLS3" 2)
[ -n "$Z1" ] && [ -n "$Z2" ] || stage_fail "COLS3 zone ids not read ($Z1 $Z2)"
if [ "$RUN" = d ]; then
    # A zone id's first eight hex digits identify it in COLS3.
    Z1K=$(echo "$Z1" | tr -d '{}' | cut -c1-8)
    Z2K=$(echo "$Z2" | tr -d '{}' | cut -c1-8)
    RZ1='{"actions":[{"value":"above","type":"setWindowLayer"}],"enabled":true,"id":"{11111111-2222-4333-8444-555555551152}","match":{"all":[{"field":"title","op":"equals","value":"l"},{"field":"zone","op":"contains","value":"'"$Z1K"'"}]},"name":"probe F1152 zone 1","priority":50}'
    RZ2='{"actions":[{"value":true,"type":"setHideTitleBar"}],"enabled":true,"id":"{11111111-2222-4333-8444-555555551153}","match":{"all":[{"field":"title","op":"equals","value":"l"},{"field":"zone","op":"contains","value":"'"$Z2K"'"}]},"name":"probe F1152 zone 2","priority":50}'
    echo "addRule zone 1 ($Z1K): $(PZ org.plasmazones.Rules.addRule "$RZ1")"
    echo "addRule zone 2 ($Z2K): $(PZ org.plasmazones.Rules.addRule "$RZ2")"
else
    R1='{"actions":[{"value":true,"type":"setHideTitleBar"}],"enabled":true,"id":"{11111111-2222-4333-8444-555555551140}","match":{"all":[{"field":"title","op":"equals","value":"l"},{"field":"isSnapped","op":"equals","value":true}]},"name":"probe F1140 snapped","priority":50}'
    echo "addRule: $(PZ org.plasmazones.Rules.addRule "$R1")"
fi
sleep 1
open l
open s
sleep 1
ids
LU=$(uuid l)
SU=$(uuid s)
[ -n "$LU" ] && [ -n "$SU" ] || stage_fail "l or s never mapped"
L="org.pz.plain|$LU"
S="org.pz.plain|$SU"
PZ org.plasmazones.Snap.moveWindowToZone "$L" "$Z1" >/dev/null
sleep 2
GL=$(geo l)
echo "staged ($RUN): l (...${LU: -8}) $GL zone='$(zoneof "$L")' | s (...${SU: -8}) $(geo s)"
if [ "$RUN" = a ]; then
    [ "$(nbof "$GL")" = true ] || stage_fail "l snapped under its own id does not read noBorder true ($GL)"
else
    KA0=$(wprop l 'w.keepAbove')
    echo "  l's zone rules before the act: keepAbove=$KA0 (zone 1 rule) | nb=$(nbof "$GL") (zone 2 rule)"
    [ "$KA0" = true ] || stage_fail "l snapped into zone 1 under its own id does not read the zone-1 rule (keepAbove $KA0)"
    [ "$(nbof "$GL")" = false ] || stage_fail "l reads the zone-2 rule before the act ($GL)"
fi
MON="$NEST/f1140-monitor.log"
mon_start "$MON" "type='signal',interface='org.plasmazones.WindowTracking',member='windowStateChanged'"
mmark "$MON"
emark
dmark
SPID=$(cpid s) || stage_fail "no test client pid for s"
activate s
[ "$(activecap)" = s ] || stage_fail "s did not take focus ($(activecap))"
echo "== s focused; daemon frozen; calls queued; s closed"
dstop || stage_fail "the daemon could not be frozen"
dsend org.plasmazones.Snap.moveWindowToZone "string:$S" "string:$Z2"
sleep 0.5
if [ "$RUN" = a ]; then
    dsend org.plasmazones.Snap.restoreWindowSize
    sleep 0.5
fi
kill "$SPID"
sleep 1.5
[ "$(wincount s)" = 0 ] || stage_fail "KWin still lists s with the daemon frozen; its close did not queue behind the calls"
dcont
sleep 3
AL=$(geo l)
echo "  l $AL"
dlog "moveWindowToZone: [^ ]*($SU|$LU).*|Bus snap of [^ ]*$SU.*|Cleaned up tracking data for closed window [^ ]*$SU.*" \
    | sed 's/org.pz.plain|[^ ]*/<id>/; s/^/    /'
NS=$(msgs "$MON" | grep "^windowStateChanged " | grep "$SU" | grep -c " snapped ")
NU=$(msgs "$MON" | grep "^windowStateChanged " | grep "$SU" | grep -c " unsnapped ")
echo "  windowStateChanged for s: snapped $NS, unsnapped $NU"
[ "$NS" -ge 1 ] || stage_fail "the monitor shows no zone notice for s"
[ "$RUN" = d ] || [ "$NU" -ge 1 ] || stage_fail "the monitor shows no unsnapped notice for s"
[ "$(dlog "Bus snap of [^ ]*$SU[^ ]* refused" | wc -l)" = 0 ] || stage_fail "the daemon refused the snap of s (it learned of the close first)"
check "the daemon's zone for l" "$(zoneof "$L")" = "$Z1"
if [ "$RUN" = d ]; then
    elog "slotWindowStateChanged: zone notice under [^ ]*$SU.*" | sed 's/org.pz.plain|[^ "]*/<id>/g; s/^/    /'
    check "effect lines refusing s's zone notice on l" \
        "$(elog "slotWindowStateChanged: zone notice under [^ ]*$SU[^ ]* not filed on [^ ]*$LU[^ ]* - its own id holds its zone" | wc -l)" -ge 1
    check "l keepAbove (the zone-1 rule)" "$(wprop l 'w.keepAbove')" = true
    check "l noBorder (the zone-2 rule does not apply)" "$(nbof "$AL")" = false
else
    check "l noBorder (its snapped rule kept)" "$(nbof "$AL")" = true
fi
finish
