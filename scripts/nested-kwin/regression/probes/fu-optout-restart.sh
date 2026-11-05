#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: GPL-3.0-or-later
#
# fu-optout-restart: a window whose snap float the tiling opt-out (autotile:none) dropped is not
# floated again when the daemon restarts. Runner: 1 output, no flags.
# Staging: Snap Assist off, Virtual-0 COLS3, w1 snapped into zone 1 and floated, then desktop 1
#   assigned autotile:none (the drop is logged). The daemon is stopped (dkill) and started again
#   (drestart, the repo's daemon.sh), so the restart's reopen sweep restores from the saved
#   session.
# Asserts: after the restart isWindowFloating w1 is false and getFloatingWindows does not list it.
# On main (8ca183308): the saved record keeps snap's floating slot, so the reopen restores w1 as a
#   float and getFloatingWindows lists it.
set -u
NEST="$1"
LOG="$2"
. "$(dirname "$0")/../lib.sh"
kwjs_ok
setting snapAssistEnabled false
assign Virtual-0 "$COLS3"
sleep 1.5
open w1
ids
W1U=$(uuid w1)
[ -n "$W1U" ] || stage_fail "w1 never mapped"
W1="org.pz.plain|$W1U"
PZ org.plasmazones.Control.snapWindowToZone "$W1" 1 Virtual-0 >/dev/null
sleep 2
floatkey w1
[ "$(floating "$W1")" = true ] || stage_fail "w1 is not snap-floating"
dmark
assignd Virtual-0 1 "autotile:none"
sleep 2.5
echo "on the opt-out: $(wstat w1 "$W1")"
[ "$(dlog "dropOptOutFloat: [^ ]*$W1U" | wc -l)" -ge 1 ] || stage_fail "the opt-out did not drop w1's float, nothing to restart over"
[ "$(floating "$W1")" = false ] || stage_fail "w1 still reads floating on the opt-out before the restart"
echo "== daemon restart"
dkill
sleep 1
drestart
dmark
sleep 6
echo "  after the restart: $(wstat w1 "$W1") | mode $(modeof Virtual-0 1)"
[ "$(modeof Virtual-0 1)" = 1 ] || stage_fail "desktop 1 lost its opt-out assignment across the restart"
check "isWindowFloating w1 after the restart" "$(floating "$W1")" = false
check "getFloatingWindows entries for w1 after the restart" "$(floatlisted "$W1")" -eq 0
finish
