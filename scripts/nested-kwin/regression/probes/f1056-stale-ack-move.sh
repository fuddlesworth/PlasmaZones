#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: GPL-3.0-or-later
#
# f1056-stale-ack-move: a size-equal move carries the stale-ack answer forward, so a superseded
# configure's late ack is re-issued over. Runner: 1 output, no flags.
# Staging: Virtual-0 scrolling with columns c1, c2, c3, c2 focused and its column set to its own
#   extent E0 in pixels. With c2's client stopped (SIGSTOP, so every command lands inside one round
#   trip): setColumnWidthPixels E0+160 (a real configure in flight), setColumnWidthPixels E0 (back
#   to the committed size, no configure), scrollViewByPx 120 (a size-equal move with a newer
#   stamp). Then SIGCONT: c2 acks the E0+160 configure.
# Asserts: no re-issue while stopped; after SIGCONT one "Re-issuing ... over a superseded
#   configure's ack" line for c2, c2 back at its pre-staging width and at the x the scroll moved it to.
# A FAIL means: the move voided the stale-ack answer and c2 kept the superseded width.
set -u
NEST="$1"
LOG="$2"
. "$(dirname "$0")/../lib.sh"
kwjs_ok
setting snapAssistEnabled false
assign Virtual-0 "scrolling:"
sleep 2
for c in c1 c2 c3; do open "$c"; done
sleep 1.5
ids
C2U=$(uuid c2)
[ -n "$C2U" ] || stage_fail "c2 never mapped"
activate c2
sleep 2
# model -> "view=V active=I extent=E extents=[...]" for Virtual-0's strip
model() {
    PZ org.plasmazones.Scrolling.stripModelJson Virtual-0 | python3 -c '
import json, sys
try:
    m = json.load(sys.stdin)
except Exception as e:
    print("unparsable:", e); sys.exit()
cols = m.get("columns", [])
a = m.get("activeColumn", -1)
ext = cols[a].get("extentPx") if 0 <= a < len(cols) else None
print("view=%s active=%s extent=%s extents=%s" % (m.get("viewOffsetPx"), a, ext, [c.get("extentPx") for c in cols]))'
}
M=$(model)
echo "strip: $M | c1 $(geo c1) | c2 $(geo c2) | c3 $(geo c3)"
E0=$(echo "$M" | grep -o "extent=[0-9.]*" | cut -d= -f2 | cut -d. -f1)
[ -n "$E0" ] || stage_fail "no active column extent ($M)"
PZ org.plasmazones.Scrolling.setColumnWidthPixels Virtual-0 "$E0" >/dev/null
sleep 2
G0=$(geo c2)
W0=$(echo "$G0" | awk '{ print int($3) }')
[ -n "$W0" ] || stage_fail "no frame for c2"
echo "c2 at a pixel column of $E0: $G0 | strip $(model)"
emark
echo "== c2 stopped; the width, the width back and the scroll inside one round trip"
cstop c2
sleep 0.3
PZ org.plasmazones.Scrolling.setColumnWidthPixels Virtual-0 $((E0 + 160)) >/dev/null
sleep 0.5
echo "  after E0+160: c2 $(geo c2) | strip $(model)"
PZ org.plasmazones.Scrolling.setColumnWidthPixels Virtual-0 "$E0" >/dev/null
sleep 0.5
echo "  after E0: c2 $(geo c2)"
V1=$(model | grep -o "view=[-0-9.]*")
PZ org.plasmazones.Scrolling.scrollViewByPx Virtual-0 120 >/dev/null
sleep 0.8
V2=$(model | grep -o "view=[-0-9.]*")
if [ "$V1" = "$V2" ]; then
    PZ org.plasmazones.Scrolling.scrollViewByPx Virtual-0 -120 >/dev/null
    sleep 0.8
    V2=$(model | grep -o "view=[-0-9.]*")
fi
GC=$(geo c2)
echo "  after the scroll: c2 $GC | $V1 -> $V2"
[ "$V1" != "$V2" ] || stage_fail "the view did not move, so the scroll made no move"
check "re-issue lines for c2 while its client is stopped" \
    "$(elog "Re-issuing .* over a superseded configure's ack .*$C2U" | wc -l)" -eq 0
ccont "$CPID"
echo "== c2 resumed"
sleep 2.5
GF=$(geo c2)
echo "  c2 $GF | strip $(model)"
elog "Re-issuing .* over a superseded configure's ack .*$C2U" | head -3 | sed 's/^/    /'
check "re-issue lines for c2 after SIGCONT" "$(elog "Re-issuing .* over a superseded configure's ack .*$C2U" | wc -l)" -eq 1
check "c2's width after SIGCONT" "$(echo "$GF" | awk '{ print int($3) }')" -eq "$W0"
check "c2's x after SIGCONT (where the scroll moved it)" "$(echo "$GF" | awk '{ print $1 }')" = "$(echo "$GC" | awk '{ print $1 }')"
finish
