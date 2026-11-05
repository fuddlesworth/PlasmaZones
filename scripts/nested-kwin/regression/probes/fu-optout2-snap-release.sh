#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: GPL-3.0-or-later
#
# fu-optout2-snap-release: a snapping desktop switched to tile with no algorithm (autotile:none)
# announces its snapped windows unsnapped in place, and switching back resnaps them.
# Runner: 1 output, no flags.
# Staging: two desktops, Snap Assist off, Virtual-0 COLS3 on both. On desktop 2, w3 is snapped into
#   zone 1. On desktop 1, w1 and w2 are snapped into zones 1 and 2 and their frames noted. Desktop 1
#   is assigned autotile:none; w1 is minimized and unminimized; desktop 1 goes back to COLS3. Then,
#   with desktop 1 in view, desktop 2 is assigned autotile:none and shown.
# Asserts: the switch logs one "releaseOptOutZones: 2 window(s) released in place on the tiling
#   opt-out Virtual-0" line and puts windowStateChanged "unsnapped" on the bus for w1 and w2; their
#   frames do not move; w1's minimize sends the daemon no float (no setWindowFloatingForScreen line
#   names w1), because the effect no longer marks it snapped. Back on COLS3, w1 is in zone 1 and w2
#   in zone 2. Desktop 2's switch announces w3 unsnapped by the time desktop 2 is shown.
# On main (8ca183308): the switch announces nothing (no releaseOptOutZones line, no "unsnapped"
#   windowStateChanged), so the effect keeps w1 and w2 marked snapped: w1's minimize sends a
#   minimize float ("setWindowFloatingForScreen: windowId= <w1> floating= true"). The frames and the
#   resnap back read the same on both builds.
set -u
NEST="$1"
LOG="$2"
. "$(dirname "$0")/../lib.sh"
kwjs_ok
setting snapAssistEnabled false
newdesk 1 D2
assign Virtual-0 "$COLS3"
sleep 1.5
Z1=$(zoneid "$COLS3" 1)
Z2=$(zoneid "$COLS3" 2)
[ -n "$Z1" ] && [ -n "$Z2" ] || stage_fail "could not read COLS3's zone ids"
desk 2
open w3
desk 1
open w1
open w2
ids
declare -A ID
for c in w1 w2 w3; do
    u=$(uuid "$c")
    [ -n "$u" ] || stage_fail "$c never mapped"
    ID[$c]="org.pz.plain|$u"
done
W1U=${ID[w1]##*|}
W2U=${ID[w2]##*|}
W3U=${ID[w3]##*|}
PZ org.plasmazones.Control.snapWindowToZone "${ID[w1]}" 1 Virtual-0 >/dev/null
sleep 1.5
PZ org.plasmazones.Control.snapWindowToZone "${ID[w2]}" 2 Virtual-0 >/dev/null
sleep 1.5
desk 2
PZ org.plasmazones.Control.snapWindowToZone "${ID[w3]}" 1 Virtual-0 >/dev/null
sleep 1.5
[ "$(zoneof "${ID[w3]}")" = "$Z1" ] || stage_fail "w3 did not snap into zone 1 on desktop 2"
desk 1
[ "$(zoneof "${ID[w1]}")" = "$Z1" ] && [ "$(zoneof "${ID[w2]}")" = "$Z2" ] \
    || stage_fail "w1 and w2 are not in zones 1 and 2"
G1=$(rect "$(geo w1)")
G2=$(rect "$(geo w2)")
echo "staged: w1 $G1 | w2 $G2"
MON="$NEST/fu-optout2-release-monitor.log"
mon_start "$MON" "type='signal',interface='org.plasmazones.WindowTracking',member='windowStateChanged'"
# unsnapped UUID: windowStateChanged signals since MMARK naming UUID with the "unsnapped" change
unsnapped() { msgs "$MON" | grep "^windowStateChanged " | grep -F "$1" | grep -c "unsnapped"; }
echo "== desktop 1 (in view) assigned autotile:none"
dmark
mmark "$MON"
assignd Virtual-0 1 "autotile:none"
sleep 2
dlog "releaseOptOutZones: .*" | sed 's/^/    /'
check "releaseOptOutZones lines for 2 windows on Virtual-0" \
    "$(dlog "releaseOptOutZones: 2 window\(s\) released in place on the tiling opt-out \"?Virtual-0\"?" | wc -l)" -eq 1
check "windowStateChanged 'unsnapped' for w1" "$(unsnapped "$W1U")" -ge 1
check "windowStateChanged 'unsnapped' for w2" "$(unsnapped "$W2U")" -ge 1
check "w1's frame after the switch (released in place)" "$(rect "$(geo w1)")" = "$G1"
check "w2's frame after the switch (released in place)" "$(rect "$(geo w2)")" = "$G2"
check "B's mode for desktop 1 (the opt-out still reads as tiling)" "$(modeof Virtual-0 1)" = 1
echo "== w1 minimized and unminimized on the opt-out"
dmark
on w1 'w[i].minimized = true;'
sleep 3
on w1 'w[i].minimized = false;'
sleep 2
dlog "setWindowFloatingForScreen: .*$W1U.*" | sed 's/^/    /'
check "setWindowFloatingForScreen lines naming w1 (no minimize float sent)" \
    "$(dlog "setWindowFloatingForScreen: .*$W1U" | wc -l)" -eq 0
check "isWindowFloating w1 after the unminimize" "$(floating "${ID[w1]}")" = false
echo "== desktop 1 back to COLS3"
assignd Virtual-0 1 "$COLS3"
sleep 2.5
echo "  w1 $(wstat w1 "${ID[w1]}") | w2 $(wstat w2 "${ID[w2]}")"
check "w1 back in zone 1" "$(zoneof "${ID[w1]}")" = "$Z1"
check "w2 back in zone 2" "$(zoneof "${ID[w2]}")" = "$Z2"
echo "== desktop 2 (hidden) assigned autotile:none, then shown"
mmark "$MON"
assignd Virtual-0 2 "autotile:none"
sleep 2
note "'unsnapped' signals for w3 while desktop 2 is hidden: $(unsnapped "$W3U")"
desk 2
sleep 1
check "windowStateChanged 'unsnapped' for w3 by its desktop's first showing" "$(unsnapped "$W3U")" -ge 1
finish
