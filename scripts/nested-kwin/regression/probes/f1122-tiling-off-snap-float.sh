#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: GPL-3.0-or-later
#
# f1122-tiling-off-snap-float: an evacuee on a hidden desktop assigned tiling while the tiling
# feature is off keeps its snap float, since that desktop runs snapping when shown. Runner: 2
# outputs, no flags.
# Staging: two desktops, Snap Assist off, autotileEnabled false (set before the assignment).
#   Virtual-1 (A) COLS3 on both desktops, Virtual-0 (B) desktop 1 COLS3 and desktop 2 autotile:bsp
#   (getModeForScreenDesktop reads 1 there). On desktop 2 w opens on A, is snapped into zone 1 and
#   floated with the float shortcut. Desktop 1 is shown, A disabled and left off: KWin moves w to B,
#   still on hidden desktop 2 (verdict 2). Desktop 2 is then shown.
# Asserts: one SnapEngine::handoffReceive for w with wasFloating= true at the settle; after the
#   switch w is floating and in no zone.
# A FAIL means: the raw autotile mode filed a tiling float pending, which snapping never owed, and
#   w came back not floating.
set -u
NEST="$1"
LOG="$2"
. "$(dirname "$0")/../lib.sh"
kwjs_ok
need_outputs 2
on_exit 'ksd output.Virtual-1.enable >/dev/null'
setting snapAssistEnabled false
newdesk 1 D2
setting autotileEnabled false
sleep 1
AE=$(PZ org.plasmazones.Settings.getSetting autotileEnabled)
echo "autotileEnabled reads $AE"
[ "$AE" = false ] || stage_fail "autotileEnabled did not switch off ($AE)"
assignd Virtual-1 1 "$COLS3"
assignd Virtual-1 2 "$COLS3"
assignd Virtual-0 1 "$COLS3"
assignd Virtual-0 2 "autotile:bsp"
sleep 1.5
echo "modes: A 1=$(modeof Virtual-1 1) 2=$(modeof Virtual-1 2) | B 1=$(modeof Virtual-0 1) 2=$(modeof Virtual-0 2) (0 snapping, 1 autotile, 2 scrolling)"
[ "$(modeof Virtual-0 2)" = 1 ] || stage_fail "B desktop 2 does not read the autotile mode ($(modeof Virtual-0 2)); the raw-mode arm is not exercised"
desk 2
echo "desktop $(curdesk)"
open w
sendto w Virtual-1 || stage_fail "w not on Virtual-1 ($(geo w))"
sleep 1.5
ids
WU=$(uuid w)
[ -n "$WU" ] || stage_fail "w never mapped"
W="org.pz.plain|$WU"
PZ org.plasmazones.Control.snapWindowToZone "$W" 1 Virtual-1 >/dev/null
sleep 2
activate w
shortcut toggle_window_float
sleep 2
echo "staged: w (...${WU: -8}) $(geo w) floating=$(floating "$W") zone='$(zoneof "$W")'"
[ "$(floating "$W")" = true ] || stage_fail "w is not snap-floating on Virtual-1"
desk 1
echo "desktop $(curdesk) shown (desktop 2 out of view on both outputs)"
echo "== Virtual-1 disabled and left off"
dmark
ksd output.Virtual-1.disable
sleep 4
echo "  outputs: $(screens)"
G=$(geo w)
echo "  w $G"
[ "$(outof "$G")" = Virtual-0 ] || stage_fail "w was not evacuated to Virtual-0 ($G)"
[ "$(deskof "$G")" = 2 ] || stage_fail "w is not on desktop 2 alone ($G); the hidden-desktop arm is not exercised"
settlerows "$WU"
[ "$(verdictof "$WU")" = 2 ] || stage_fail "w's settle row is not verdict 2 ($(verdictof "$WU"))"
H=$(handoffs "$WU")
[ -n "$H" ] && echo "$H" | sed 's/^/    /'
check "handoffReceive wasFloating= true lines for w at the settle" "$(grep -c "wasFloating= true" <<< "$H")" -eq 1
echo "== desktop 2 shown on Virtual-0"
desk 2
sleep 2
echo "  desktop $(curdesk): w $(geo w) | B desktop 2 mode $(modeof Virtual-0 2) (raw)"
check "isWindowFloating w" "$(floating "$W")" = true
check "w's zone" "$(zoneof "$W")" = ""
finish
