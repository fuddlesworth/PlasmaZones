#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: GPL-3.0-or-later
#
# f488-tiled-not-snapped: a tiled window is not treated as snapped by window rules or by snap
# focus-follows-mouse. Runner: 1 output, flag F.
# Staging: Virtual-0 COLS3, Snap Assist off, snap focus-follows-mouse on, autotile
#   focus-follows-mouse off. Two rules on p1: isSnapped true -> setHideTitleBar (noBorder), zone
#   contains COLS3 zone 1's id -> setWindowLayer above (keepAbove). p1 is snapped into zone 1, p2
#   into zone 2. Virtual-0 is switched to autotile:bsp; p2 is activated and the pointer is moved
#   over p1 with fake input. Then Virtual-0 goes back to COLS3.
# Asserts: tiled, neither rule matches p1 (noBorder and keepAbove false) and the pointer over p1
#   leaves p2 active; back on COLS3, p1 is in zone 1 again with both rules matching. Both rules
#   matching the snapped p1 first is a staging precondition.
# A FAIL means: the rules and snap focus-follows-mouse still read the tile as snapped, so p1 kept
#   its hidden title bar and layer and the pointer activated it.
set -u
NEST="$1"
LOG="$2"
. "$(dirname "$0")/../lib.sh"
kwjs_ok
Z1="{2bb43b1d-3506-40b7-8f2b-e0fe59c0e90e}"
Z2="{93b382be-8b48-496b-b53c-88bef2c46787}"
setting snapAssistEnabled false
setting snappingFocusFollowsMouse true
setting autotileFocusFollowsMouse false
assign Virtual-0 "$COLS3"
sleep 2
R1='{"actions":[{"value":true,"type":"setHideTitleBar"}],"enabled":true,"id":"{11111111-2222-4333-8444-555555557a01}","match":{"all":[{"field":"title","op":"equals","value":"p1"},{"field":"isSnapped","op":"equals","value":true}]},"name":"probe F488 snapped","priority":50}'
R2='{"actions":[{"value":"above","type":"setWindowLayer"}],"enabled":true,"id":"{11111111-2222-4333-8444-555555557a02}","match":{"all":[{"field":"title","op":"equals","value":"p1"},{"field":"zone","op":"contains","value":"2bb43b1d"}]},"name":"probe F488 zone","priority":50}'
[ "$(PZ org.plasmazones.Rules.addRule "$R1")" = true ] || stage_fail "the isSnapped rule was refused"
[ "$(PZ org.plasmazones.Rules.addRule "$R2")" = true ] || stage_fail "the zone rule was refused"
sleep 1
open p1 300 300
open p2 300 300
ids
P1=$(uuid p1)
P2=$(uuid p2)
[ -n "$P1" ] && [ -n "$P2" ] || stage_fail "p1 or p2 never mapped"
# p1rules -> "noBorder=B keepAbove=B active=CAPTION" for p1
p1rules() { wprop p1 '"noBorder=" + w.noBorder + " keepAbove=" + w.keepAbove + " active=" + (workspace.activeWindow ? workspace.activeWindow.caption : "none")'; }
PZ org.plasmazones.Snap.moveWindowToZone "org.pz.plain|$P1" "$Z1" >/dev/null
PZ org.plasmazones.Snap.moveWindowToZone "org.pz.plain|$P2" "$Z2" >/dev/null
sleep 2
[ "$(zoneof "org.pz.plain|$P1")" = "$Z1" ] || stage_fail "p1 did not snap into zone 1"
SNAPPED=$(rect "$(geo p1)")
echo "p1 snapped in zone 1: $SNAPPED $(p1rules)"
[ "$(p1rules | cut -d' ' -f1,2)" = "noBorder=true keepAbove=true" ] || stage_fail "the rules do not match the snapped p1 ($(p1rules))"
assign Virtual-0 "autotile:bsp"
sleep 3
echo "p1 tiled: $(geo p1) $(p1rules)"
check "p1's rule state while tiled" "$(p1rules | cut -d' ' -f1,2)" = "noBorder=false keepAbove=false"
activate p2
sleep 0.4
[ "$(activecap)" = p2 ] || stage_fail "p2 could not be activated"
read -r X Y W H _ <<< "$(geo p1)"
CX=$((${X%.*} + ${W%.*} / 2))
CY=$((${Y%.*} + ${H%.*} / 2))
fake abs $((CX - 40)) "$CY" wait 150 path $((CX - 40)) "$CY" "$CX" "$CY" 8 30 wait 300 rel 3 0 wait 300
sleep 1
check "the active window after the pointer moves over the tiled p1" "$(activecap)" = p2
assign Virtual-0 "$COLS3"
sleep 3
echo "back on COLS3: $(geo p1) $(p1rules)"
check "p1's zone back on COLS3" "$(zoneof "org.pz.plain|$P1")" = "$Z1"
check "p1's frame back on COLS3 (zone 1's rect)" "$(rect "$(geo p1)")" = "$SNAPPED"
check "p1's rule state back on COLS3" "$(p1rules | cut -d' ' -f1,2)" = "noBorder=true keepAbove=true"
finish
