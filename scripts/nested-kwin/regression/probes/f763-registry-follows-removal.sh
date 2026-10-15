#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: GPL-3.0-or-later
#
# f763-registry-follows-removal: the window registry renumbers a window's desktops when KWin
# removes a desktop below it. Runner: 1 output, no flags.
# Staging: four desktops, COLS3 snapping. dw (a static-title window that never re-pushes its
#   metadata) opens on desktop 3 and is snapped into zone 2 there. Desktop 1 is shown, KWin
#   removes desktop 2 (dw is now on desktop 2), and the user switches 2, 1, 2.
# Asserts: the registry logs "remapDesktops: renumbered the desktops of N window(s)"; no
#   windowDesktopMoveRequested names dw and no release line does; on every switch dw stays on
#   KWin desktop 2 at its zone-2 frame with zone 2 kept.
# A FAIL means: the registry kept dw's old desktop number, so a later metadata push or move
#   request sends it to the wrong desktop.
set -u
NEST="$1"
LOG="$2"
. "$(dirname "$0")/../lib.sh"
kwjs_ok
for n in 1 2 3; do
    newdesk "$n" "D$((n + 1))"
    sleep 0.5
done
setting snapAssistEnabled false
assign Virtual-0 "$COLS3"
sleep 2
Z2=$(zoneid "$COLS3" 2)
[ -n "$Z2" ] || stage_fail "COLS3 has no zone 2"
desk 3
open dw 300 300
ids
DW=$(uuid dw)
[ -n "$DW" ] || stage_fail "dw never mapped"
ID="org.pz.plain|$DW"
PZ org.plasmazones.Snap.moveWindowToZone "$ID" "$Z2" >/dev/null
sleep 2
G=$(geo dw)
echo "dw in zone 2 on desktop 3: $G zone=$(zoneof "$ID")"
[ "$(zoneof "$ID")" = "$Z2" ] || stage_fail "dw did not snap into zone 2"
[ "$(deskof "$G")" = 3 ] || stage_fail "dw is not on desktop 3 ($G)"
R=$(rect "$G")
desk 1
MON="$NEST/f763r-monitor.log"
mon_start "$MON" "type='signal',interface='org.plasmazones.WindowTracking',member='windowDesktopMoveRequested'"
mmark "$MON"
dmark
ID2=$(deskid 2)
[ -n "$ID2" ] || stage_fail "could not read desktop 2's id"
echo "removing desktop 2 ($ID2)"
qdbus6 org.kde.KWin /VirtualDesktopManager org.kde.KWin.VirtualDesktopManager.removeDesktop "$ID2" >/dev/null
sleep 2.5
for d in 2 1 2; do
    desk "$d"
    G=$(geo dw)
    echo "on desktop $(curdesk): dw $G zone=$(zoneof "$ID")"
    check "dw's KWin desktop with desktop $d shown" "$(deskof "$G")" = 2
    check "dw's frame with desktop $d shown" "$(rect "$G")" = "$R"
    check "dw's zone with desktop $d shown" "$(zoneof "$ID")" = "$Z2"
done
sleep 1
check "windowDesktopMoveRequested signals naming dw" "$(named "$MON" windowDesktopMoveRequested "$DW")" -eq 0
check "registry remap lines at the removal" "$(dlog "remapDesktops: renumbered the desktops of [1-9][0-9]* window" | wc -l)" -ge 1
check "release lines naming dw" "$(dlog "released .*$DW" | wc -l)" -eq 0
finish
