#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: GPL-3.0-or-later
#
# fu-optout-minimize: a minimize of a window kept snapped from before stores no float on the
# tiling opt-out (autotile:none), and the window never reads floating. Runner: 1 output, no flags.
# Staging: Snap Assist off, Virtual-0 COLS3, w snapped into zone 1, then desktop 1 assigned
#   autotile:none (w's zone stays as frozen snap memory). w is minimized, left minimized past the
#   snap minimize-float debounce, then unminimized.
# Asserts: no "setWindowFloatingForScreen: windowId= <w> floating= true" line, so no minimize float
#   is stored (either the effect sends none, because the switch announced w unsnapped, see
#   fu-optout2-snap-release, or the opt-out gate refuses it with "... - nothing floats there");
#   isWindowFloating w is false while minimized and after; no windowFloatingChanged says w is
#   floating.
# On main (8ca183308): the minimize float is stored ("setWindowFloatingForScreen: windowId= <w>
#   floating= true") and windowFloatingChanged(w, true) goes out.
set -u
NEST="$1"
LOG="$2"
. "$(dirname "$0")/../lib.sh"
kwjs_ok
setting snapAssistEnabled false
assign Virtual-0 "$COLS3"
sleep 1.5
open w
ids
WU=$(uuid w)
[ -n "$WU" ] || stage_fail "w never mapped"
W="org.pz.plain|$WU"
PZ org.plasmazones.Control.snapWindowToZone "$W" 1 Virtual-0 >/dev/null
sleep 2
case "$(zoneof "$W")" in "{"*) ;; *) stage_fail "w did not snap into zone 1" ;; esac
assignd Virtual-0 1 "autotile:none"
sleep 2
echo "on the opt-out: $(wstat w "$W")"
MON="$NEST/fu-optout-minimize-monitor.log"
mon_start "$MON" "type='signal',interface='org.plasmazones.WindowTracking',member='windowFloatingChanged'"
mmark "$MON"
dmark
on w 'w[i].minimized = true;'
sleep 3
echo "  minimized: $(wstat w "$W")"
check "isWindowFloating w while minimized" "$(floating "$W")" = false
on w 'w[i].minimized = false;'
sleep 2
echo "  unminimized: $(wstat w "$W") | windowFloatingChanged [$(wfc "$MON" "$WU")]"
dlog "setWindowFloatingForScreen: .*$WU.*" | sed 's/^/    /'
check "minimize floats stored for w" \
    "$(dlog "setWindowFloatingForScreen: windowId= [^ ]*$WU[^ ]* floating= true" | wc -l)" -eq 0
check "isWindowFloating w after the unminimize" "$(floating "$W")" = false
check "windowFloatingChanged signals saying w floats" "$(wfct "$MON" "$WU")" -eq 0
finish
