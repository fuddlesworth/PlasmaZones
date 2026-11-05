#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: GPL-3.0-or-later
#
# f622-drag-desktop-switch: the F622 drag hold under global desktops, where KWin carries a dragged
# window through a desktop switch. A control for the hold's other side. Runner: 1 output, flags F.
# Staging: two desktops, Virtual-0 COLS3, Snap Assist off. p1 is snapped into zone 1 on desktop 1
#   and Meta-dragged; the desktop switches to 2 mid-drag and p1 is dropped free.
# Asserts: after the drop p1 is on desktop 2, the desktop in view, with no zone. (Mid-drag p1
#   on desktop 2 and still moving is the staging.)
set -u
NEST="$1"
LOG="$2"
. "$(dirname "$0")/../lib.sh"
kwjs_ok
newdesk 1 Two
setting snapAssistEnabled false
assign Virtual-0 "$COLS3"
sleep 2
open p1 300 300
ids
P1U=$(uuid p1)
[ -n "$P1U" ] || stage_fail "p1 never mapped"
P1="org.pz.plain|$P1U"
Z1=$(zoneid "$COLS3" 1)
[ -n "$Z1" ] || stage_fail "no zone 1 in COLS3"
PZ org.plasmazones.Snap.moveWindowToZone "$P1" "$Z1" >/dev/null
sleep 2
echo "p1 snapped: $(geo p1) zone=$(zoneof "$P1")"
case "$(zoneof "$P1")" in "{"*) ;; *) stage_fail "p1 did not snap into COLS3 zone 1" ;; esac
fake abs 170 300 wait 100 keydown 125 wait 50 down left wait 150 \
    path 170 300 600 300 15 30 wait 2500 path 600 300 650 320 5 30 wait 300 up left wait 80 keyup 125 &
FIPID=$!
sleep 1.6
js 'workspace.currentDesktop = workspace.desktops[1];'
sleep 0.5
# One read: the drop follows about a second later.
MID=$(wprop p1 'w.desktops.map(function (d) { return d.x11DesktopNumber; }) + " cur=" + workspace.currentDesktop.x11DesktopNumber + " move=" + w.move')
echo "mid-drag after the switch: $MID"
[ "$MID" = "2 cur=2 move=true" ] || stage_fail "KWin did not carry the dragged p1 through the switch ($MID)"
wait "$FIPID"
sleep 2.5
G=$(geo p1)
echo "p1 after the drop: $G cur=$(curdesk) zone='$(zoneof "$P1")'"
check "p1's desktops after the drop" "$(deskof "$G")" = 2
check "the desktop in view after the drop" "$(curdesk)" = "2 of 2"
check "p1's zone after the free drop" "$(zoneof "$P1")" '!~' '[0-9a-f]{8}-'
finish
