#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: GPL-3.0-or-later
#
# fu-optout-cross-screen-move: a floating window that KWin moves onto a monitor showing the tiling
# opt-out (autotile:none) lands not floating, and is a free window when it goes back.
# Runner: 2 outputs, no flags.
# Staging: one desktop, Snap Assist off. Virtual-1 (A) COLS3, Virtual-0 (B) autotile:none. w opens
#   on A, is snapped into zone 1 and floated with the float shortcut; w2 opens on A and is floated
#   with the shortcut without ever snapping (a screenless snap float). Each is moved to B with
#   KWin's Move to Screen, then w back to A.
# Asserts, for w and for w2 on B: one "windowScreenChanged: floating window <id> moved onto the
#   tiling opt-out" line, isWindowFloating false, not listed by getFloatingWindows, no zone, and
#   the last windowFloatingChanged does not say true. w back on A: not floating, no zone.
# On main (8ca183308): no opt-out line; snap re-homes the window floating on B ("handoff
#   complete"). The float reads stay the same on both builds while the window sits on the opt-out,
#   because both getters go through the router lens, so the missing line is what fails there.
set -u
NEST="$1"
LOG="$2"
. "$(dirname "$0")/../lib.sh"
kwjs_ok
need_outputs 2
setting snapAssistEnabled false
assign Virtual-1 "$COLS3"
assignd Virtual-0 1 "autotile:none"
sleep 1.5
for c in w w2; do
    open "$c"
    sendto "$c" Virtual-1 || stage_fail "$c not on Virtual-1 ($(geo "$c"))"
done
sleep 1
ids
WU=$(uuid w)
W2U=$(uuid w2)
[ -n "$WU" ] && [ -n "$W2U" ] || stage_fail "w or w2 never mapped"
W="org.pz.plain|$WU"
W2="org.pz.plain|$W2U"
PZ org.plasmazones.Control.snapWindowToZone "$W" 1 Virtual-1 >/dev/null
sleep 2
floatkey w
floatkey w2
echo "staged: w $(wstat w "$W") | w2 $(wstat w2 "$W2")"
[ "$(floating "$W")" = true ] || stage_fail "w is not snap-floating on Virtual-1"
[ "$(floating "$W2")" = true ] || stage_fail "the float shortcut did not float the never-snapped w2"
MON="$NEST/fu-optout-move-monitor.log"
mon_start "$MON" "type='signal',interface='org.plasmazones.WindowTracking',member='windowFloatingChanged'"
for c in w w2; do
    id="$W"
    u="$WU"
    [ "$c" = w2 ] && id="$W2" && u="$W2U"
    echo "== $c moved to Virtual-0 by KWin"
    dmark
    mmark "$MON"
    sendto "$c" Virtual-0 || stage_fail "$c did not reach Virtual-0 ($(geo "$c"))"
    sleep 1.5
    echo "  $(wstat "$c" "$id") | windowFloatingChanged [$(wfc "$MON" "$u")]"
    dlog "windowScreenChanged: .*$u.*" | head -3 | sed 's/^/    /'
    check "$c: 'moved onto the tiling opt-out' lines" \
        "$(dlog "windowScreenChanged: floating window [^ ]*$u[^ ]* moved onto the tiling opt-out" | wc -l)" -eq 1
    check "$c: isWindowFloating on the opt-out" "$(floating "$id")" = false
    check "$c: getFloatingWindows entries on the opt-out" "$(floatlisted "$id")" -eq 0
    check "$c: zone on the opt-out" "$(zoneof "$id")" = ""
    check "$c: the last windowFloatingChanged" "$(wfclast "$MON" "$u")" != true
done
echo "== w moved back to Virtual-1"
sendto w Virtual-1 || stage_fail "w did not reach Virtual-1 ($(geo w))"
sleep 1.5
echo "  $(wstat w "$W")"
check "w back on Virtual-1: isWindowFloating" "$(floating "$W")" = false
check "w back on Virtual-1: zone" "$(zoneof "$W")" = ""
finish
