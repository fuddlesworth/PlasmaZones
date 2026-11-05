#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: GPL-3.0-or-later
#
# fu-optout-float-verbs: the float verbs do nothing to a window on the tiling opt-out
# (autotile:none). Runner: 2 outputs, no flags.
# Staging: Snap Assist off. Virtual-1 runs autotile:bsp, so the autotile engine is enabled and the
#   shortcuts' generic gate ("Float shortcut: ignored — engine disabled for screen") does not
#   decide first: with no screen running autotile, that gate refuses on both builds and the
#   opt-out gate is never reached. Virtual-0 desktop 1 is autotile:none, w is open there (a free
#   window) and focused. Then, in turn: the float shortcut (toggle_window_float), the restore
#   shortcut (restore_window_size), WindowTracking.setWindowFloating(w, true) and
#   Snap.toggleFloatForWindow(w, Virtual-0).
# Asserts: the float shortcut logs "Float shortcut: ignored, <w> is on the tiling opt-out" and w
#   stays not floating; the restore shortcut logs "Restore shortcut: ignored ..." and w does not
#   move; the bus setWindowFloating logs "setWindowFloatingForScreen: <w> is on the tiling opt-out
#   ... - nothing floats there" and w stays not floating; toggleFloatForWindow logs "Bus float of
#   <w> refused: snapping does not run".
# On main (8ca183308), in this staging: the float and restore shortcuts pass the generic gate and
#   reach the autotile engine, which does not track w and answers with an OSD failure
#   (navigationFeedback "window_not_tracked"); no "is on the tiling opt-out" line is logged. The
#   bus setWindowFloating has no opt-out line. The toggleFloatForWindow refusal predates this
#   branch and may read the same.
set -u
NEST="$1"
LOG="$2"
. "$(dirname "$0")/../lib.sh"
kwjs_ok
need_outputs 2
setting snapAssistEnabled false
assign Virtual-1 "autotile:bsp"
assignd Virtual-0 1 "autotile:none"
sleep 1.5
[ "$(modeof Virtual-0 1)" = 1 ] || stage_fail "desktop 1 does not read as tiling after the opt-out assignment"
open w
sendto w Virtual-0 || stage_fail "w is not on Virtual-0 ($(geo w))"
ids
WU=$(uuid w)
[ -n "$WU" ] || stage_fail "w never mapped"
W="org.pz.plain|$WU"
activate w
[ "$(activecap)" = w ] || stage_fail "w is not the active window"
echo "staged: $(wstat w "$W")"
[ "$(floating "$W")" = false ] || stage_fail "w reads floating before any verb"
MON="$NEST/fu-optout-verbs-monitor.log"
mon_start "$MON" "type='signal',interface='org.plasmazones.WindowTracking',member='navigationFeedback'"
echo "== a. the float shortcut"
dmark
mmark "$MON"
shortcut toggle_window_float
sleep 2
[ "$(dlog "Float shortcut: ignored .{1,3} engine disabled" | wc -l)" = 0 ] \
    || stage_fail "the generic engine gate refused the float shortcut, so the opt-out gate was not reached"
check "a. 'Float shortcut: ignored' lines for w" \
    "$(dlog "Float shortcut: ignored, [^ ]*$WU[^ ]* is on the tiling opt-out" | wc -l)" -ge 1
check "a. isWindowFloating w" "$(floating "$W")" = false
echo "  navigationFeedback since a.: [$(msgs "$MON" | grep "^navigationFeedback " | tr '\n' ';')]"
check "a. 'window_not_tracked' feedback from the autotile engine" \
    "$(msgs "$MON" | grep "^navigationFeedback " | grep -c "window_not_tracked")" -eq 0
echo "== b. the restore shortcut"
G0=$(rect "$(geo w)")
dmark
activate w
shortcut restore_window_size
sleep 2
[ "$(dlog "Restore shortcut: ignored .{1,3} engine disabled" | wc -l)" = 0 ] \
    || stage_fail "the generic engine gate refused the restore shortcut, so the opt-out gate was not reached"
check "b. 'Restore shortcut: ignored' lines for w" \
    "$(dlog "Restore shortcut: ignored, [^ ]*$WU[^ ]* is on the tiling opt-out" | wc -l)" -ge 1
check "b. w's frame after the restore shortcut" "$(rect "$(geo w)")" = "$G0"
echo "== c. WindowTracking.setWindowFloating(w, true)"
dmark
echo "  reply: '$(PZ org.plasmazones.WindowTracking.setWindowFloating "$W" true)'"
sleep 1.5
check "c. 'nothing floats there' lines for w" \
    "$(dlog "setWindowFloatingForScreen: [^ ]*$WU[^ ]* is on the tiling opt-out.*nothing floats there" | wc -l)" -ge 1
check "c. isWindowFloating w" "$(floating "$W")" = false
echo "== d. Snap.toggleFloatForWindow(w, Virtual-0)"
dmark
echo "  reply: '$(PZ org.plasmazones.Snap.toggleFloatForWindow "$W" Virtual-0)'"
sleep 1.5
check "d. 'refused: snapping does not run' lines for w" \
    "$(dlog "Bus float of [^ ]*$WU[^ ]* refused: snapping does not run" | wc -l)" -ge 1
check "d. isWindowFloating w" "$(floating "$W")" = false
finish
