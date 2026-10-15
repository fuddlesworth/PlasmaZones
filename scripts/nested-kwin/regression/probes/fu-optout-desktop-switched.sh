#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: GPL-3.0-or-later
#
# fu-optout-desktop-switched: a snapping desktop switched to the tiling opt-out (autotile:none)
# drops the snap floats on it, in view at once and out of view when shown. Runner: 1 output, no
# flags.
# Staging: two desktops, Snap Assist off, Virtual-0 COLS3 on both. On desktop 2, w3 is snapped
#   into zone 2 and floated. On desktop 1, w1 and w2 are snapped (zones 1 and 3) and w1 floated.
#   Desktop 1 is assigned autotile:none, then COLS3 again; the float shortcut is pressed twice on
#   w1. Then, with desktop 1 in view, desktop 2 is assigned autotile:none and shown.
# Asserts: the in-view switch logs one "dropOptOutFloat: <w1> holds no float on the tiling
#   opt-out" and none for w2; w1 reads not floating, is not in getFloatingWindows and the last
#   windowFloatingChanged for it does not say true; w2 keeps its zone. Back on COLS3, w1 is a free
#   window: not floating, the first float shortcut floats it, the second does not put it back in
#   zone 1; w2 is still in zone 3. Showing desktop 2 after its switch logs the drop for w3, which
#   reads not floating.
# On main (8ca183308): no dropOptOutFloat line; w1 and w3 keep snap's float, so getFloatingWindows
#   lists them and the effect keeps them floating.
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
Z3=$(zoneid "$COLS3" 3)
[ -n "$Z1" ] && [ -n "$Z3" ] || stage_fail "could not read COLS3's zone ids"
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
PZ org.plasmazones.Control.snapWindowToZone "${ID[w2]}" 3 Virtual-0 >/dev/null
sleep 1.5
floatkey w1
desk 2
PZ org.plasmazones.Control.snapWindowToZone "${ID[w3]}" 2 Virtual-0 >/dev/null
sleep 1.5
floatkey w3
[ "$(floating "${ID[w3]}")" = true ] || stage_fail "w3 is not snap-floating on desktop 2"
desk 1
echo "staged: w1 $(wstat w1 "${ID[w1]}") | w2 $(wstat w2 "${ID[w2]}")"
[ "$(floating "${ID[w1]}")" = true ] || stage_fail "w1 is not snap-floating on desktop 1"
[ "$(zoneof "${ID[w2]}")" = "$Z3" ] || stage_fail "w2 is not in zone 3"
MON="$NEST/fu-optout-switch-monitor.log"
mon_start "$MON" "type='signal',interface='org.plasmazones.WindowTracking',member='windowFloatingChanged'"
echo "== desktop 1 (in view) assigned autotile:none"
dmark
mmark "$MON"
assignd Virtual-0 1 "autotile:none"
sleep 2
echo "  w1 $(wstat w1 "${ID[w1]}") | w2 $(wstat w2 "${ID[w2]}") | windowFloatingChanged w1 [$(wfc "$MON" "$W1U")]"
dlog "dropOptOutFloat: .*" | sed 's/^/    /'
check "dropOptOutFloat lines for w1" "$(dlog "dropOptOutFloat: [^ ]*$W1U[^ ]* holds no float on the tiling opt-out" | wc -l)" -eq 1
check "dropOptOutFloat lines for w2" "$(dlog "dropOptOutFloat: [^ ]*$W2U" | wc -l)" -eq 0
check "isWindowFloating w1 on the opt-out" "$(floating "${ID[w1]}")" = false
check "getFloatingWindows entries for w1" "$(floatlisted "${ID[w1]}")" -eq 0
check "the last windowFloatingChanged for w1" "$(wfclast "$MON" "$W1U")" != true
check "w2's zone on the opt-out (kept as snap memory)" "$(zoneof "${ID[w2]}")" = "$Z3"
echo "== desktop 1 back to COLS3"
assignd Virtual-0 1 "$COLS3"
sleep 2
echo "  w1 $(wstat w1 "${ID[w1]}") | w2 $(wstat w2 "${ID[w2]}")"
check "w1 back on COLS3: isWindowFloating" "$(floating "${ID[w1]}")" = false
floatkey w1
check "w1 after the first float shortcut: isWindowFloating" "$(floating "${ID[w1]}")" = true
floatkey w1
echo "  w1 after the second float shortcut: $(wstat w1 "${ID[w1]}")"
check "w1 after the second float shortcut: zone" "$(zoneof "${ID[w1]}")" != "$Z1"
check "w2 still in zone 3" "$(zoneof "${ID[w2]}")" = "$Z3"
echo "== desktop 2 (hidden) assigned autotile:none, then shown"
dmark
assignd Virtual-0 2 "autotile:none"
sleep 2
note "dropOptOutFloat lines for w3 while desktop 2 is hidden: $(dlog "dropOptOutFloat: [^ ]*$W3U" | wc -l)"
desk 2
echo "  w3 $(wstat w3 "${ID[w3]}")"
check "dropOptOutFloat lines for w3 by its first showing" "$(dlog "dropOptOutFloat: [^ ]*$W3U[^ ]* holds no float" | wc -l)" -ge 1
check "isWindowFloating w3 on the shown opt-out" "$(floating "${ID[w3]}")" = false
check "getFloatingWindows entries for w3" "$(floatlisted "${ID[w3]}")" -eq 0
finish
