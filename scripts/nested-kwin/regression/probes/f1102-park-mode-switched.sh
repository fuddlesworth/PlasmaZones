#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: GPL-3.0-or-later
#
# f1102-park-mode-switched: a desktop given another mode while its monitor was away gets nothing
# back from the tiling engine that parked it. Runner: 2 outputs, no flags, PZ_F1102_RUN=scroll
# (f1102-park-mode-switched-scroll) or tile (f1102-park-mode-switched-tile).
# Staging: two desktops, Snap Assist off. Virtual-1 (A) "scrolling:" or autotile:bsp on both
#   desktops, Virtual-0 (B) COLS3. On desktop 2 c1 and c2 open on A. Desktop 1 is shown. A's screen
#   id is read before the unplug. A is disabled (c1, c2 land on B on hidden desktop 2, verdict 2);
#   while A is away A desktop 2 is assigned COLS3; A is enabled with desktop 1 in view, then desktop
#   2 is shown.
# Asserts: no "<Scroll|Autotile>Engine::readoptParked: <id> re-seated" line for c1 or c2 and neither
#   return row is verdict 1 Readopt. scroll: with A desktop 2 put back to scrolling in view, the
#   strip lists each window once.
# A FAIL means: the parking engine re-seated the windows into a desktop that snaps now.
set -u
NEST="$1"
LOG="$2"
. "$(dirname "$0")/../lib.sh"
RUN="${PZ_F1102_RUN:-scroll}"
kwjs_ok
need_outputs 2
case "$RUN" in
scroll)
    MODE="scrolling:"
    ENG=Scroll
    ;;
tile)
    MODE="autotile:bsp"
    ENG=Autotile
    ;;
*) stage_fail "unknown PZ_F1102_RUN '$RUN'" ;;
esac
on_exit 'ksd output.Virtual-1.enable >/dev/null'
setting snapAssistEnabled false
newdesk 1 D2
assignd Virtual-1 1 "$MODE"
assignd Virtual-1 2 "$MODE"
assign Virtual-0 "$COLS3"
sleep 1.5
AID=$(screenid Virtual-1)
[ -n "$AID" ] || stage_fail "Screen.getScreenId Virtual-1 answered nothing"
echo "A's screen id '$AID' | modes: A 1=$(modeof "$AID" 1) 2=$(modeof "$AID" 2) | B 1=$(modeof Virtual-0 1)"
desk 2
for c in c1 c2; do
    open "$c"
    sendto "$c" Virtual-1 || stage_fail "$c not on Virtual-1 ($(geo "$c"))"
done
sleep 2
ids
C1U=$(uuid c1)
C2U=$(uuid c2)
[ -n "$C1U" ] && [ -n "$C2U" ] || stage_fail "c1 and c2 did not both map"
if [ "$RUN" = scroll ]; then
    echo "staged on desktop $(curdesk): strip $(striporder Virtual-1)"
else
    echo "staged on desktop $(curdesk): tiles $(tileorder Virtual-1)"
fi
desk 1
echo "desktop $(curdesk) shown"
MON="$NEST/f1102-monitor.log"
mon_start "$MON" "type='signal',interface='org.plasmazones.Tiling',member='windowsTileRequested'"
echo "== Virtual-1 disabled"
dmark
ksd output.Virtual-1.disable
sleep 4
echo "  c1 $(geo c1) | c2 $(geo c2)"
settlerows "$C1U"
settlerows "$C2U"
[ "$(verdictof "$C1U")" = 2 ] && [ "$(verdictof "$C2U")" = 2 ] || stage_fail "the unplug rows are not verdict 2"
echo "== A desktop 2 assigned COLS3 while A is away"
assignd "$AID" 2 "$COLS3"
sleep 1.5
[ "$(modeof "$AID" 2)" = 0 ] || stage_fail "A desktop 2 does not read snapping after the assignment ($(modeof "$AID" 2))"
echo "== Virtual-1 enabled again, desktop 1 in view"
dmark
mmark "$MON"
ksd output.Virtual-1.enable
sleep 6
echo "  outputs: $(screens)"
[ "$(modeof Virtual-1 2)" = 0 ] || stage_fail "A desktop 2 does not read snapping after the return ($(modeof Virtual-1 2))"
echo "  c1 $(geo c1) | c2 $(geo c2)"
settlerows "$C1U"
settlerows "$C2U"
check "${ENG}Engine re-seated lines for c1" "$(reseated "$ENG" "$C1U")" -eq 0
check "${ENG}Engine re-seated lines for c2" "$(reseated "$ENG" "$C2U")" -eq 0
check "c1's return row verdict" "$(verdictof "$C1U")" != 1
check "c2's return row verdict" "$(verdictof "$C2U")" != 1
echo "== desktop 2 shown (it snaps now)"
mmark "$MON"
desk 2
sleep 2.5
echo "  desktop $(curdesk): c1 $(geo c1) | c2 $(geo c2)"
note "windowsTileRequested batches naming c1, c2 since the switch: $(named "$MON" windowsTileRequested "$C1U") $(named "$MON" windowsTileRequested "$C2U")"
note "zones: c1 '$(zoneof "org.pz.plain|$C1U")' c2 '$(zoneof "org.pz.plain|$C2U")'"
if [ "$RUN" = scroll ]; then
    echo "== A desktop 2 put back to scrolling, in view"
    assignd Virtual-1 2 "scrolling:"
    sleep 3
    S=$(striporder Virtual-1)
    echo "  strip: $S"
    check "c1 appears once in the strip" "$(grep -o "c1" <<< "$S" | wc -l)" -eq 1
    check "c2 appears once in the strip" "$(grep -o "c2" <<< "$S" | wc -l)" -eq 1
fi
finish
