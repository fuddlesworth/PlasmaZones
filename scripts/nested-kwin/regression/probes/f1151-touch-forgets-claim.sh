#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: GPL-3.0-or-later
#
# f1151-touch-forgets-claim: a touch that drops an evacuee's park for a returned output the effect
# holds no record for still takes the window's Monocle claim for that output with it. Runner: 2
# outputs, no flags.
# Staging: two desktops, per-output desktops off, Snap Assist off. Virtual-1 (A) autotile:monocle on
#   both desktops, Virtual-0 (B) COLS3 on both. w and w2 open on A desktop 1 (two tiles, so the batch
#   is monocle and w a member), and w is put on desktops 1 and 2. A is disabled with desktop 1 in
#   view: both land on B (verdict 2, the claim filed for Virtual-1). While A is away A desktop 2 is
#   given COLS3. Desktop 2 is shown and A enabled untouched: KWin returns w, and since A's view runs
#   snapping w's desktop-1 context is granted for later (parkReseated, no parkDropped), and the
#   Readopt verdict's prune erases w's record for A.
# Touch: w is sent to B and snapped into zone 1 there: the daemon logs "dropEvacueeParks: <w> touched"
#   and emits parkDropped(w, A) with A connected, outside any settle and announce. Then desktop 1 is
#   shown and w is sent back to A, whose monocle batch names it.
# Asserts: at the touch the effect logs "Evacuee park on Virtual-1 forgotten for <w> monocle claim:
#   true"; at A's batch no "Evacuee monocle claim of <w> taken back on Virtual-1".
# A FAIL means: the effect returned before its forget when it found no record, and the claim kept for
#   A survived for the next monocle batch there.
set -u
NEST="$1"
LOG="$2"
. "$(dirname "$0")/../lib.sh"
kwjs_ok
need_outputs 2
on_exit 'ksd output.Virtual-1.enable >/dev/null'
setting snapAssistEnabled false
newdesk 1 D2
assignd Virtual-1 1 "autotile:monocle"
assignd Virtual-1 2 "autotile:monocle"
assignd Virtual-0 1 "$COLS3"
assignd Virtual-0 2 "$COLS3"
sleep 1.5
desk 1
emark
for c in w w2; do
    open "$c"
    sendto "$c" Virtual-1 || stage_fail "$c not on Virtual-1 ($(geo "$c"))"
done
sleep 2
ids
WU=$(uuid w)
[ -n "$WU" ] || stage_fail "w never mapped"
W="org.pz.plain|$WU"
on w 'w[i].desktops = [workspace.desktops[0], workspace.desktops[1]];'
sleep 2
G=$(geo w)
echo "staged: w (...${WU: -8}) $G | w2 $(geo w2) | w's last tile request monocle=$(monocleof "$WU")"
[ "$(deskof "$G")" = 1,2 ] || stage_fail "w is not on desktops 1 and 2 ($G)"
[ "$(monocleof "$WU")" = true ] || stage_fail "A's last batch for w is not monocle"
[ "$(maxof "$G")" = 3 ] || stage_fail "w is not Monocle-maximized on Virtual-1 ($G)"
MON="$NEST/f1151-monitor.log"
mon_start "$MON" "type='signal',interface='org.plasmazones.WindowTracking',member='parkDropped'" \
    "type='signal',interface='org.plasmazones.WindowTracking',member='parkReseated'"
echo "== Virtual-1 disabled, desktop 1 in view"
emark
dmark
ksd output.Virtual-1.disable
sleep 4
echo "  w $(geo w) | w2 $(geo w2)"
settlerows "$WU"
[ "$(outof "$(geo w)")" = Virtual-0 ] || stage_fail "w was not evacuated to Virtual-0"
elog "Evacuee [^ ]*$WU.*" | sed 's/org.pz.plain|[^ "]*/<w>/g; s/^/    /'
check "effect filed w's Monocle claim for Virtual-1" \
    "$(elog "Evacuee [^ ]*$WU[^ ]* parked for [^ ]*Virtual-1[^ ]* monocle claim: true" | wc -l)" -ge 1
echo "== while A is away: A desktop 2 given COLS3 (snapping)"
assignd Virtual-1 2 "$COLS3"
sleep 1.5
echo "  modes: A 1=$(modeof Virtual-1 1) 2=$(modeof Virtual-1 2)"
desk 2
echo "== desktop $(curdesk) shown; Virtual-1 enabled untouched"
emark
dmark
mmark "$MON"
ksd output.Virtual-1.enable
sleep 6
echo "  w $(geo w) | w2 $(geo w2)"
settlerows "$WU"
V=$(verdictof "$WU")
NR=$(named "$MON" parkReseated "$WU")
ND=$(named "$MON" parkDropped "$WU")
msgs "$MON" | grep "$WU" | sed 's/org.pz.plain|[^ ]*/<w>/; s/^/    /'
[ "$(outof "$(geo w)")" = Virtual-1 ] || stage_fail "KWin did not return w to A ($(geo w))"
[ "$V" = 1 ] || stage_fail "w's return row is not verdict 1 (${V:-no row}); the record prune the probe needs did not run"
[ "$NR" -ge 1 ] && [ "$ND" = 0 ] || stage_fail "the return did not leave a place granted for later (parkReseated $NR, parkDropped $ND)"
echo "== the touch: w sent to B and snapped into zone 1 there"
emark
dmark
mmark "$MON"
sendto w Virtual-0 || stage_fail "w not on Virtual-0 ($(geo w))"
sleep 1.5
PZ org.plasmazones.Control.snapWindowToZone "$W" 1 Virtual-0 >/dev/null
sleep 2.5
echo "  w $(geo w) zone=$(zoneof "$W")"
elog "(Evacuee|Park drop for) .*$WU.*" | sed 's/org.pz.plain|[^ "]*/<w>/g; s/^/    /'
[ "$(dlog "dropEvacueeParks: [^ ]*$WU[^ ]* touched" | wc -l)" -ge 1 ] || stage_fail "the daemon logged no touch for w"
[ "$(named "$MON" parkDropped "$WU")" -ge 1 ] || stage_fail "the touch dropped no park for w"
check "forget lines for w's claim on Virtual-1 at the touch" \
    "$(elog "Evacuee park on [^ ]*Virtual-1[^ ]* forgotten for [^ ]*$WU[^ ]* monocle claim: true" | wc -l)" -eq 1
echo "== desktop 1 shown; w sent back to A (a monocle batch there names it)"
desk 1
emark
sendto w Virtual-1 || stage_fail "w did not reach Virtual-1 ($(geo w))"
sleep 3
echo "  w $(geo w) | A tiles $(tileorder Virtual-1) | w's last tile request monocle=$(monocleof "$WU")"
elog "Evacuee monocle claim of [^ ]*$WU.*" | sed 's/org.pz.plain|[^ "]*/<w>/g; s/^/    /'
[ "$(monocleof "$WU")" = true ] || stage_fail "no monocle tile request for w on A; the claim arm is not reached"
check "'taken back on Virtual-1' lines for w at A's batch" \
    "$(elog "Evacuee monocle claim of [^ ]*$WU[^ ]* taken back on [^ ]*Virtual-1" | wc -l)" -eq 0
finish
