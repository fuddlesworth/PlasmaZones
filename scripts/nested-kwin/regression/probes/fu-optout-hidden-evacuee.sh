#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: GPL-3.0-or-later
#
# fu-optout-hidden-evacuee: a snap float evacuated onto a monitor whose own desktop for it is hidden
# and assigned autotile:none is not stated a float there, and reads not floating when that desktop
# is shown. Runner: 2 outputs, no flags.
# Staging: two desktops (global), Snap Assist off. Virtual-1 (A) COLS3 on both; Virtual-0 (B)
#   COLS3 on desktop 1 and autotile:none on desktop 2. w opens on desktop 2, is sent to A, snapped
#   into zone 1 and floated. Desktop 1 is shown, then A is disabled, so KWin moves w onto B with
#   w's desktop out of view (settle verdict 2). Desktop 2 is shown; with w untouched, B's desktop 2
#   is switched from the opt-out to COLS3, so w sits in a snapping context without moving. Then A
#   is enabled again.
# Asserts: while desktop 2 is hidden getFloatingWindows does not list w; shown, isWindowFloating w
#   is false and getFloatingWindows still does not list it; B's desktop 2 still reads as tiling.
#   Once B's desktop 2 snaps, w still reads not floating and is not listed. After A returns:
#   verdict 1 (Readopt), w back on A and floating.
# On main (8ca183308): on the opt-out the reads are the same (both getters go through the router
#   lens, which asks the autotile engine there), but the settle states w a snap float in B's
#   desktop-2 store (F1122), so once that desktop snaps isWindowFloating reads true and
#   getFloatingWindows lists w.
set -u
NEST="$1"
LOG="$2"
. "$(dirname "$0")/../lib.sh"
kwjs_ok
need_outputs 2
on_exit 'ksd output.Virtual-1.enable >/dev/null'
setting snapAssistEnabled false
newdesk 1 D2
assign Virtual-1 "$COLS3"
assignd Virtual-0 1 "$COLS3"
assignd Virtual-0 2 "autotile:none"
sleep 1.5
echo "modes: B 1=$(modeof Virtual-0 1) 2=$(modeof Virtual-0 2) | A 2=$(modeof Virtual-1 2)"
desk 2
open w
sendto w Virtual-1 || stage_fail "w not on Virtual-1 ($(geo w))"
sleep 1.5
ids
WU=$(uuid w)
[ -n "$WU" ] || stage_fail "w never mapped"
W="org.pz.plain|$WU"
[ "$(deskof "$(geo w)")" = 2 ] || stage_fail "w is not on desktop 2 only ($(geo w))"
PZ org.plasmazones.Control.snapWindowToZone "$W" 1 Virtual-1 >/dev/null
sleep 2
floatkey w
echo "staged on desktop 2: $(wstat w "$W")"
[ "$(floating "$W")" = true ] || stage_fail "w is not snap-floating on Virtual-1"
desk 1
echo "== desktop $(curdesk) shown; Virtual-1 disabled"
dmark
ksd output.Virtual-1.disable
sleep 4
echo "  w $(geo w)"
settlerows "$WU"
handoffs "$WU" | sed 's/^/    /'
[ "$(outof "$(geo w)")" = Virtual-0 ] || stage_fail "KWin did not move w onto Virtual-0"
V=$(verdictof "$WU")
[ "$V" = 2 ] || stage_fail "w's settle row is not verdict 2 (${V:-no row})"
check "getFloatingWindows entries for w while its desktop is hidden" "$(floatlisted "$W")" -eq 0
echo "== desktop 2 shown"
desk 2
echo "  $(wstat w "$W")"
check "isWindowFloating w on the shown opt-out" "$(floating "$W")" = false
check "getFloatingWindows entries for w on the shown opt-out" "$(floatlisted "$W")" -eq 0
check "B's mode for desktop 2 (the opt-out still reads as tiling)" "$(modeof Virtual-0 2)" = 1
echo "== B's desktop 2 switched from the opt-out to COLS3, w untouched"
G0=$(rect "$(geo w)")
dmark
assignd Virtual-0 2 "$COLS3"
sleep 2
echo "  $(wstat w "$W") | mode $(modeof Virtual-0 2)"
dlog "windowsReleased: .*$WU.*|readopt.*$WU.*|handoffReceive: .*$WU.*" | sed 's/^/    /'
[ "$(modeof Virtual-0 2)" = 0 ] || stage_fail "B's desktop 2 does not read as snapping after the COLS3 assignment"
[ "$(rect "$(geo w)")" = "$G0" ] && [ "$(outof "$(geo w)")" = Virtual-0 ] \
    || stage_fail "the assignment switch moved w ($(geo w)), so the read below is not of the untouched evacuee"
check "isWindowFloating w once B's desktop 2 snaps (no snap float survived the opt-out)" "$(floating "$W")" = false
check "getFloatingWindows entries for w once B's desktop 2 snaps" "$(floatlisted "$W")" -eq 0
echo "== Virtual-1 enabled again, w untouched"
dmark
ksd output.Virtual-1.enable
sleep 5
echo "  w $(geo w)"
settlerows "$WU"
check "w's settle row at the return" "$(verdictof "$WU")" = 1
check "w's output after the return" "$(outof "$(geo w)")" = Virtual-1
check "isWindowFloating w back on Virtual-1" "$(floating "$W")" = true
finish
