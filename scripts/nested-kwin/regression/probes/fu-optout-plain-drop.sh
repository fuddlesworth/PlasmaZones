#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: GPL-3.0-or-later
#
# fu-optout-plain-drop: a window the user drags onto the tiling opt-out (autotile:none) with no
# trigger held lands not floating, from a snapping source and from a tiling one.
# Runner: 2 outputs, flag F.
# Staging: one desktop, Snap Assist off, Virtual-0 (B) autotile:none. Part 1: Virtual-1 (A) COLS3,
#   w snapped into zone 1 there (not floated), then Meta+left dragged 700 px left onto B and
#   dropped (no snap trigger). Part 2: A switched to autotile:bsp, w3 opens there as a tile, is
#   floated with the float shortcut, then dragged onto B the same way. Last, with both left
#   untouched on B, B's desktop 1 is switched from the opt-out to COLS3, so they sit in a snapping
#   context without moving.
# Asserts: w on B has no zone, isWindowFloating false and is not in getFloatingWindows; w3 on B
#   reads not floating, is not in getFloatingWindows, and A's tiles do not name it. Once B snaps,
#   neither reads floating nor is listed.
# On main (8ca183308): on the opt-out the reads are the same, because both getters go through the
#   router lens, which asks the autotile engine there; but snap took both floats at the drops
#   ("SnapEngine::handoffReceive ... wasFloating= true"), so once B snaps both read floating and
#   getFloatingWindows lists them.
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
echo "== part 1: a snapped window dropped onto the opt-out"
open w
sendto w Virtual-1 || stage_fail "w not on Virtual-1 ($(geo w))"
sleep 1
ids
WU=$(uuid w)
[ -n "$WU" ] || stage_fail "w never mapped"
W="org.pz.plain|$WU"
PZ org.plasmazones.Control.snapWindowToZone "$W" 1 Virtual-1 >/dev/null
sleep 2
echo "  staged: $(wstat w "$W")"
case "$(zoneof "$W")" in "{"*) ;; *) stage_fail "w did not snap into zone 1 on Virtual-1" ;; esac
touchdrag w -700 0
sleep 1
echo "  after the drop: $(wstat w "$W")"
[ "$(outof "$(geo w)")" = Virtual-0 ] || stage_fail "the drag did not land w on Virtual-0 ($(geo w))"
check "w after the drop: zone" "$(zoneof "$W")" = ""
check "w after the drop: isWindowFloating" "$(floating "$W")" = false
check "w after the drop: getFloatingWindows entries" "$(floatlisted "$W")" -eq 0
echo "== part 2: a tiling float dropped onto the opt-out"
assign Virtual-1 "autotile:bsp"
sleep 2
open w3
sendto w3 Virtual-1 || stage_fail "w3 not on Virtual-1 ($(geo w3))"
sleep 1.5
ids
W3U=$(uuid w3)
[ -n "$W3U" ] || stage_fail "w3 never mapped"
W3="org.pz.plain|$W3U"
case "$(tilelist Virtual-1)" in *"${W3U: -8}"*) ;; *) stage_fail "w3 is not one of Virtual-1's tiles" ;; esac
floatkey w3
echo "  staged: $(wstat w3 "$W3") | A tiles $(tilelist Virtual-1)"
[ "$(floating "$W3")" = true ] || stage_fail "the float shortcut did not float w3 out of A's tiles"
touchdrag w3 -700 0
sleep 1
echo "  after the drop: $(wstat w3 "$W3") | A tiles $(tilelist Virtual-1)"
[ "$(outof "$(geo w3)")" = Virtual-0 ] || stage_fail "the drag did not land w3 on Virtual-0 ($(geo w3))"
check "w3 after the drop: isWindowFloating" "$(floating "$W3")" = false
check "w3 after the drop: getFloatingWindows entries" "$(floatlisted "$W3")" -eq 0
check "A's tiles after w3 left" "$(tilelist Virtual-1)" '!~' "${W3U: -8}"
echo "== B's desktop 1 switched from the opt-out to COLS3, w and w3 untouched"
GW=$(rect "$(geo w)")
GW3=$(rect "$(geo w3)")
dmark
assignd Virtual-0 1 "$COLS3"
sleep 2
echo "  w $(wstat w "$W") | w3 $(wstat w3 "$W3") | mode $(modeof Virtual-0 1)"
dlog "windowsReleased: .*($WU|$W3U).*|handoffReceive: .*($WU|$W3U).*" | sed 's/^/    /'
[ "$(modeof Virtual-0 1)" = 0 ] || stage_fail "B's desktop 1 does not read as snapping after the COLS3 assignment"
[ "$(rect "$(geo w)")" = "$GW" ] && [ "$(rect "$(geo w3)")" = "$GW3" ] \
    || stage_fail "the assignment switch moved w or w3, so the reads below are not of the untouched drops"
check "w once B snaps: isWindowFloating (no snap float survived the drop)" "$(floating "$W")" = false
check "w once B snaps: getFloatingWindows entries" "$(floatlisted "$W")" -eq 0
check "w3 once B snaps: isWindowFloating (no snap float survived the drop)" "$(floating "$W3")" = false
check "w3 once B snaps: getFloatingWindows entries" "$(floatlisted "$W3")" -eq 0
finish
