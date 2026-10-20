#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: GPL-3.0-or-later
#
# f1064-axis-fullscreen-restore-rect: the effect pushes a free window's restore rect on an
# axis-only maximize (F1064) and on fullscreen (F1065). Runner: 1 output, no flags.
# Staging: Virtual-0 COLS3, Snap Assist off, f1 (plain_gtk.py) free at P = (200,150), a dbus-monitor
#   on WindowTracking.storePreTileGeometry. Step 1: setMaximize(true, false) (vertical only). Step 2:
#   unmaximized, moved to P2 = (500,250), fullScreen = true. Step 3: fullscreen off, snapped into
#   zone 1, toggle_window_float.
# Asserts: step 1 sends one storePreTileGeometry for f1 at 200 150 with overwrite true; step 2 sends
#   one at 500 250; step 3 floats f1 back to P2.
# A FAIL means: the axis-only edge was cancelled before the push, or the fullscreen window was refused
#   before it.
set -u
NEST="$1"
LOG="$2"
. "$(dirname "$0")/../lib.sh"
kwjs_ok
setting snapAssistEnabled false
assign Virtual-0 "$COLS3"
sleep 1.5
open f1
moveto f1 200 150
sleep 1.5
ids
F1U=$(uuid f1)
[ -n "$F1U" ] || stage_fail "f1 never mapped"
F1="org.pz.plain|$F1U"
echo "staged: f1 $(geo f1) zone='$(zoneof "$F1")'"
[ "$(rect "$(geo f1)" | awk '{ print $1, $2 }')" = "200 150" ] || stage_fail "f1 did not move to 200,150"
MON="$NEST/f1064-monitor.log"
mon_start "$MON" "type='method_call',member='storePreTileGeometry'"
pushes() { msgs "$MON" | grep "^storePreTileGeometry .*$F1U"; }
echo "== step 1: vertical maximize"
mmark "$MON"
on f1 'w[i].setMaximize(true, false);'
sleep 2
echo "  f1 $(geo f1)"
[ "$(maxof "$(geo f1)")" = 1 ] || stage_fail "the vertical maximize did not take"
P=$(pushes)
echo "  calls: ${P:-none}" | sed 's/org.pz.plain|[^ ]*/<f1>/g'
check "storePreTileGeometry calls for f1 at 200 150 with overwrite" "$(echo "$P" | grep -c " 200 150 .* true$")" -eq 1
on f1 'w[i].setMaximize(false, false);'
sleep 1.5
moveto f1 500 250
sleep 1.5
echo "== step 2: fullscreen from P2 $(geo f1)"
[ "$(rect "$(geo f1)" | awk '{ print $1, $2 }')" = "500 250" ] || stage_fail "f1 did not move to 500,250"
P2=$(geo f1)
mmark "$MON"
on f1 'w[i].fullScreen = true;'
sleep 2
echo "  f1 $(geo f1)"
P=$(pushes)
echo "  calls: ${P:-none}" | sed 's/org.pz.plain|[^ ]*/<f1>/g'
check "storePreTileGeometry calls for f1 at 500 250" "$(echo "$P" | grep -c " 500 250 ")" -eq 1
echo "== step 3: fullscreen off, snap into zone 1, float"
on f1 'w[i].fullScreen = false;'
sleep 2
PZ org.plasmazones.Control.snapWindowToZone "$F1" 1 Virtual-0 >/dev/null
sleep 2
activate f1
shortcut toggle_window_float
sleep 2
check "f1 floated back to P2" "$(rect "$(geo f1)")" = "$(rect "$P2")"
check "f1 floating" "$(floating "$F1")" = true
finish
