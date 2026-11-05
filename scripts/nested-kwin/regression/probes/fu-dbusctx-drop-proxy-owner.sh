#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: GPL-3.0-or-later
#
# fu-dbusctx-drop-proxy-owner: a drop proxy is retired when the process that registered it leaves
# the bus. Runner: 1 output, flag F.
# Staging: Snap Assist off, Virtual-0 COLS3, p open. clients/dropproxy_owner.py registers a proxy
#   on Virtual-0 whose one cell covers 400,200 200x200 (inside COLS3's zone 2) and maps it to zone
#   1, then holds its bus connection. p is Alt-dragged to 500,300 and dropped: the proxy sends it
#   into zone 1. The owner process is killed, then p is Alt-dragged to 500,300 again.
# Asserts: the daemon logs "drop proxy owner <name> went away; retired the proxy on Virtual-0" for
#   the owner's bus name; the second drop lands p in zone 2, the real zone under the pointer.
# On main (8ca183308): the proxy is registered unowned (the adaptor read no caller), so no line is
#   logged and the second drop still snaps p into zone 1, until the daemon restarts.
set -u
NEST="$1"
LOG="$2"
. "$(dirname "$0")/../lib.sh"
kwjs_ok
setting snapAssistEnabled false
assign Virtual-0 "$COLS3"
sleep 1.5
Z1=$(zoneid "$COLS3" 1)
Z2=$(zoneid "$COLS3" 2)
[ -n "$Z1" ] && [ -n "$Z2" ] || stage_fail "could not read COLS3's zone ids"
open p 300 300
ids
PU=$(uuid p)
[ -n "$PU" ] || stage_fail "p never mapped"
P="org.pz.plain|$PU"
PROXY='{"rect":[400,200,200,200],"cells":[{"id":"'"$Z1"'","rect":[400,200,200,200]}]}'
OUTF="$NEST/fu-dropproxy-owner.out"
python3 "$PZR_CLIENTS/dropproxy_owner.py" Virtual-0 "$PROXY" > "$OUTF" 2>&1 < /dev/null &
OPID=$!
on_exit "kill $OPID 2>/dev/null"
OWNER=""
for _ in $(seq 20); do
    OWNER=$(sed -n 's/^registered //p' "$OUTF" | head -1)
    [ -n "$OWNER" ] && break
    sleep 0.5
done
[ -n "$OWNER" ] || stage_fail "the proxy owner did not register ($(head -c 200 "$OUTF"))"
echo "proxy owner: $OWNER (pid $OPID)"
echo "== drop 1, the owner alive"
altdrag p 500 300
echo "  p $(geo p) zone=$(zoneof "$P")"
[ "$(zoneof "$P")" = "$Z1" ] || stage_fail "the first drop did not go through the proxy into zone 1 ($(zoneof "$P"))"
echo "== the owner exits"
dmark
kill "$OPID"
for _ in $(seq 20); do
    kill -0 "$OPID" 2>/dev/null || break
    sleep 0.25
done
sleep 1.5
dlog "drop proxy owner.*" | sed 's/^/    /'
check "the daemon retired the owner's proxy on Virtual-0" \
    "$(dlog "drop proxy owner \"?$OWNER\"? went away; retired the proxy on \"?Virtual-0\"?" | wc -l)" -eq 1
echo "== drop 2, the owner gone"
altdrag p 500 300
echo "  p $(geo p) zone=$(zoneof "$P")"
check "p's zone after the second drop (the real zone under the pointer)" "$(zoneof "$P")" = "$Z2"
finish
