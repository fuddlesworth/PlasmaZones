#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: GPL-3.0-or-later
#
# f1105-hidden-evacuee-mode: an evacuee on a hidden desktop floats in the mode of its own desktop,
# not the one in view on the screen it landed on. Runner: 2 outputs, no flags,
# PZ_F1105_RUN=tile (f1105-hidden-evacuee-mode-tile) or snap (f1105-hidden-evacuee-mode-snap).
# Staging: two desktops, Snap Assist off. Virtual-1 (A) COLS3 on both desktops. On desktop 2 w opens
#   on A, is snapped into zone 1 and floated with the float shortcut (a snap float). Desktop 1 is
#   shown and A disabled and left off: KWin moves w to Virtual-0 (B), still on hidden desktop 2
#   (verdict 2). Desktop 2 is then shown.
#   tile: B desktop 1 COLS3 (in view at the settle), B desktop 2 autotile:bsp holding one tile t1.
#   snap: B desktop 1 autotile:bsp (in view at the settle), B desktop 2 COLS3.
# Asserts: tile: no SnapEngine::handoffReceive for w at the settle; at the switch to desktop 2 a
#   floating= true send for w, w floating, t1's frame untouched and w not among B's tiles.
#   snap: one handoffReceive for w with wasFloating= true at the settle; w floating on desktop 2 in
#   no zone.
# A FAIL means: the settle stated the float into the mode in view (tile: w tiled beside t1; snap:
#   the float filed pending for tiling and lost).
set -u
NEST="$1"
LOG="$2"
. "$(dirname "$0")/../lib.sh"
RUN="${PZ_F1105_RUN:-tile}"
kwjs_ok
need_outputs 2
case "$RUN" in
tile)
    B1="$COLS3"
    B2="autotile:bsp"
    ;;
snap)
    B1="autotile:bsp"
    B2="$COLS3"
    ;;
*) stage_fail "unknown PZ_F1105_RUN '$RUN'" ;;
esac
on_exit 'ksd output.Virtual-1.enable >/dev/null'
setting snapAssistEnabled false
newdesk 1 D2
assignd Virtual-1 1 "$COLS3"
assignd Virtual-1 2 "$COLS3"
assignd Virtual-0 1 "$B1"
assignd Virtual-0 2 "$B2"
sleep 1.5
echo "modes: A 1=$(modeof Virtual-1 1) 2=$(modeof Virtual-1 2) | B 1=$(modeof Virtual-0 1) 2=$(modeof Virtual-0 2) (0 snapping, 1 autotile, 2 scrolling)"
desk 2
echo "desktop $(curdesk)"
if [ "$RUN" = tile ]; then
    open t1
    sendto t1 Virtual-0 || stage_fail "t1 not on Virtual-0 ($(geo t1))"
fi
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
if [ "$RUN" = tile ]; then
    T1G=$(geo t1)
    echo "  t1 on B desktop 2: $T1G | tiles $(tiles Virtual-0)"
fi
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
if [ "$RUN" = tile ]; then
    check "handoffReceive lines for w at the settle" "$(grep -c "handoffReceive" <<< "$H")" -eq 0
else
    check "handoffReceive wasFloating= true lines for w at the settle" "$(grep -c "wasFloating= true" <<< "$H")" -eq 1
fi
echo "== desktop 2 shown on Virtual-0"
dmark
desk 2
sleep 2
echo "  desktop $(curdesk): w $(geo w)"
dlog "setWindowFloatingForScreen: windowId= [^ ]*$WU.*" | sed 's/org.pz.plain|[^ ]*/<w>/; s/^/    /'
if [ "$RUN" = tile ]; then
    check "floating= true sends for w since the switch" "$(floatsends "$WU")" -ge 1
    check "isWindowFloating w" "$(floating "$W")" = true
    check "t1's frame untouched" "$(rect "$(geo t1)")" = "$(rect "$T1G")"
    check "w is not one of Virtual-0's tiles" "$(tileorder Virtual-0)" '!~' '(^| )w( |$)'
else
    check "isWindowFloating w" "$(floating "$W")" = true
    check "w's zone" "$(zoneof "$W")" = ""
fi
finish
