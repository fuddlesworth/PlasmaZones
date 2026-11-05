#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: GPL-3.0-or-later
#
# fu-optout-inview-evacuee: a snap float evacuated onto an in-view desktop assigned autotile:none
# (the tiling opt-out, where no engine owns a float) lands not floating, and keeps its park on the
# monitor it left. Runner: 2 outputs, no flags.
# Staging: one desktop, Snap Assist off. Virtual-1 (A) COLS3; w opens there, is snapped into zone
#   1 and floated with the float shortcut. Virtual-0 (B), desktop 1 in view, is assigned
#   autotile:none. A is disabled with kscreen-doctor, so KWin moves w onto B (settle verdict 2,
#   EvacueeFloat). Then, with w untouched on B, B's desktop 1 is switched from the opt-out to
#   COLS3, so w sits in a snapping context without moving. A is then enabled again.
# Asserts: on the opt-out, isWindowFloating w is false, getFloatingWindows does not list w, w has
#   no zone, the last windowFloatingChanged for w does not say true, and B's mode still reads 1
#   (autotile). Once B snaps, w still reads not floating and is not listed. After A returns:
#   settle verdict 1 (Readopt), w back on A and floating (the park survived).
# On main (8ca183308): on the opt-out every read is the same, because both getters go through
#   the router lens, which asks the autotile engine there; but the snap engine took the float at
#   the landing ("SnapEngine::handoffReceive ... wasFloating= true"), so once B snaps the lens asks
#   snap and isWindowFloating reads true and getFloatingWindows lists w.
set -u
NEST="$1"
LOG="$2"
. "$(dirname "$0")/../lib.sh"
kwjs_ok
need_outputs 2
on_exit 'ksd output.Virtual-1.enable >/dev/null'
setting snapAssistEnabled false
assign Virtual-1 "$COLS3"
assignd Virtual-0 1 "autotile:none"
sleep 1.5
echo "desktop $(curdesk) | modes: A=$(modeof Virtual-1 1) B=$(modeof Virtual-0 1) (0 snapping, 1 autotile)"
[ "$(modeof Virtual-0 1)" = 1 ] || stage_fail "B's desktop 1 does not read as tiling after the opt-out assignment"
open w
sendto w Virtual-1 || stage_fail "w not on Virtual-1 ($(geo w))"
sleep 1.5
ids
WU=$(uuid w)
[ -n "$WU" ] || stage_fail "w never mapped"
W="org.pz.plain|$WU"
PZ org.plasmazones.Control.snapWindowToZone "$W" 1 Virtual-1 >/dev/null
sleep 2
floatkey w
echo "staged: $(wstat w "$W")"
[ "$(floating "$W")" = true ] || stage_fail "w is not snap-floating on Virtual-1"
MON="$NEST/fu-optout-inview-monitor.log"
mon_start "$MON" "type='signal',interface='org.plasmazones.WindowTracking',member='windowFloatingChanged'"
echo "== Virtual-1 disabled, desktop 1 in view on Virtual-0"
dmark
mmark "$MON"
ksd output.Virtual-1.disable
sleep 4
echo "  w $(geo w)"
settlerows "$WU"
handoffs "$WU" | sed 's/^/    /'
[ "$(outof "$(geo w)")" = Virtual-0 ] || stage_fail "KWin did not move w onto Virtual-0"
V=$(verdictof "$WU")
[ "$V" = 2 ] || stage_fail "w's settle row is not verdict 2 (${V:-no row})"
echo "  windowFloatingChanged for w since the disable: [$(wfc "$MON" "$WU")]"
check "isWindowFloating w on the opt-out" "$(floating "$W")" = false
check "getFloatingWindows entries for w on the opt-out" "$(floatlisted "$W")" -eq 0
check "w's zone on the opt-out" "$(zoneof "$W")" = ""
check "the last windowFloatingChanged for w" "$(wfclast "$MON" "$WU")" != true
check "B's mode for desktop 1 (the opt-out still reads as tiling)" "$(modeof Virtual-0 1)" = 1
echo "== B's desktop 1 switched from the opt-out to COLS3, w untouched"
G0=$(rect "$(geo w)")
dmark
assignd Virtual-0 1 "$COLS3"
sleep 2
echo "  $(wstat w "$W") | mode $(modeof Virtual-0 1)"
dlog "windowsReleased: .*$WU.*|readopt.*$WU.*|handoffReceive: .*$WU.*" | sed 's/^/    /'
[ "$(modeof Virtual-0 1)" = 0 ] || stage_fail "B's desktop 1 does not read as snapping after the COLS3 assignment"
[ "$(rect "$(geo w)")" = "$G0" ] && [ "$(outof "$(geo w)")" = Virtual-0 ] \
    || stage_fail "the assignment switch moved w ($(geo w)), so the read below is not of the untouched evacuee"
check "isWindowFloating w once B snaps (no snap float survived the opt-out)" "$(floating "$W")" = false
check "getFloatingWindows entries for w once B snaps" "$(floatlisted "$W")" -eq 0
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
