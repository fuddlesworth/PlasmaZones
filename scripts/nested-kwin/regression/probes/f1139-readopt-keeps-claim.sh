#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: GPL-3.0-or-later
#
# f1139-readopt-keeps-claim: an evacuee's Monocle claim is filed for the output it left (F1148) and,
# when KWin returns it with that output (a Readopt), the park drop inside the settle keeps the claim
# for the re-seat batch to take back. Runner: 2 outputs, no flags.
# Staging: Snap Assist off. Virtual-1 (A) and Virtual-0 (B) autotile:monocle. w and w2 open on A (the
#   engine flags monocle only with two tiles or more), so Monocle maximizes w and the effect holds it
#   as a member. A is disabled: both land on B untouched (verdict 2). A is enabled with w untouched:
#   KWin returns it (verdict 1).
# Asserts: at the removal the effect logs "Evacuee <w> parked for Virtual-1 monocle claim: true"; at
#   the return "Park drop for <w> on Virtual-1 inside the settle: its verdict decides", then "Evacuee
#   monocle claim of <w> taken back on Virtual-1".
# A FAIL means: the claim was never filed, or the drop inside a Readopt's settle forgot it like a
#   touch, so the returning member lost its claim.
set -u
NEST="$1"
LOG="$2"
. "$(dirname "$0")/../lib.sh"
kwjs_ok
need_outputs 2
on_exit 'ksd output.Virtual-1.enable >/dev/null'
claimlines() { elog "(Evacuee|Park drop for|Park re-seat for) .*$1.*" | sed 's/org.pz.plain|[^ "]*/<w>/g; s/^/    /'; }
setting snapAssistEnabled false
assign Virtual-1 "autotile:monocle"
assign Virtual-0 "autotile:monocle"
sleep 2
emark
for c in w w2; do
    open "$c"
    sendto "$c" Virtual-1 || stage_fail "$c not on Virtual-1 ($(geo "$c"))"
done
sleep 2
ids
WU=$(uuid w)
W2U=$(uuid w2)
[ -n "$WU" ] && [ -n "$W2U" ] || stage_fail "w or w2 never mapped"
G=$(geo w)
echo "staged: w (...${WU: -8}) $G | w2 $(geo w2) | w's last tile request monocle=$(monocleof "$WU")"
[ "$(monocleof "$WU")" = true ] || stage_fail "A's last batch for w is not monocle"
[ "$(maxof "$G")" = 3 ] || stage_fail "w is not Monocle-maximized on Virtual-1 ($G)"
echo "== Virtual-1 disabled, w and w2 untouched"
emark
dmark
ksd output.Virtual-1.disable
sleep 4
echo "  w $(geo w) | w2 $(geo w2)"
settlerows "$WU"
settlerows "$W2U"
[ "$(outof "$(geo w)")" = Virtual-0 ] || stage_fail "w was not evacuated to Virtual-0"
claimlines "$WU"
check "effect filed w's Monocle claim for Virtual-1" \
    "$(elog "Evacuee [^ ]*$WU[^ ]* parked for [^ ]*Virtual-1[^ ]* monocle claim: true" | wc -l)" -ge 1
echo "== Virtual-1 enabled again, w untouched"
emark
dmark
ksd output.Virtual-1.enable
sleep 6
echo "  w $(geo w) | w2 $(geo w2)"
settlerows "$WU"
[ "$(verdictof "$WU")" = 1 ] || stage_fail "w's return row is not verdict 1 ($(verdictof "$WU")); no Readopt to keep the claim for"
claimlines "$WU"
check "park drops for w on Virtual-1 held inside the settle" \
    "$(elog "Park drop for [^ ]*$WU[^ ]* on [^ ]*Virtual-1[^ ]* inside the settle: its verdict decides" | wc -l)" -ge 1
check "w's Monocle claim taken back on Virtual-1" \
    "$(elog "Evacuee monocle claim of [^ ]*$WU[^ ]* taken back on [^ ]*Virtual-1" | wc -l)" -ge 1
finish
