#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: GPL-3.0-or-later
#
# f1011-monocle-arrival-unmaximize: a column-maximized strip tile handed to a Monocle stack keeps
# its restore rect on that monitor, so a user un-maximize leaves it there floating. Runner: 2
# outputs; f1011-monocle-arrival-unmaximize-wayland (no flags) and -x11 (flag X,
# PZ_F1011_CLIENT=x11).
# Staging: Virtual-1 autotile:monocle holding m2. Virtual-0 scrolling with one column w (Wayland
#   or X11 per PZ_F1011_CLIENT). Scrolling.toggleMaximizeToEdges maximizes w's column, then
#   move_window_right (w active) carries it onto Virtual-1, where w and m2 are both
#   Monocle-maximized. Then the user's setMaximize(false, false) on w.
# Asserts: w stays on Virtual-1 0.4 s and 2 s after the un-maximize, logs one "Monocle window
#   manually unmaximized" and floats, with no "Window moved between monitors" line for it.
# A FAIL means: the un-maximize sent w back to its strip frame on Virtual-0.
set -u
NEST="$1"
LOG="$2"
CLIENT="${PZ_F1011_CLIENT:-wayland}"
. "$(dirname "$0")/../lib.sh"
kwjs_ok
need_outputs 2
assign Virtual-1 "autotile:monocle"
sleep 2
open m2
sendto m2 Virtual-1 || stage_fail "m2 not on Virtual-1 ($(geo m2))"
assign Virtual-0 "scrolling:"
sleep 2
if [ "$CLIENT" = x11 ]; then
    xopen w
else
    open w
fi
sleep 0.5
[ "$(outof "$(geo w)")" = Virtual-0 ] || sendto w Virtual-0 || stage_fail "w not on Virtual-0"
activate w
ids
WU=$(uuid w)
[ -n "$WU" ] || stage_fail "w never mapped"
WID=$(grep -oh "[A-Za-z0-9._-]*|$WU" "$NEST/daemon.log" "$LOG" | head -1)
[ -n "$WID" ] || WID="org.pz.plain|$WU"
echo "w ($CLIENT) id: $WID; in the strip: $(frame w)"
echo "toggleMaximizeToEdges: $(PZ org.plasmazones.Scrolling.toggleMaximizeToEdges Virtual-0 "$WID")"
sleep 2
case "$(frame w)" in *"@Virtual-0 max=3") ;; *) stage_fail "w's column did not maximize ($(frame w))" ;; esac
activate w
[ "$(activecap)" = w ] || stage_fail "w is not active before the move"
shortcut move_window_right
sleep 4.5
SW=$(frame w)
SM=$(frame m2)
echo "after move_window_right: w $SW | m2 $SM"
case "$SW|$SM" in
*"@Virtual-1 max=3|"*"@Virtual-1 max=3") ;;
*) stage_fail "no monocle on Virtual-1" ;;
esac
emark
on w 'w[i].setMaximize(false, false);'
check "w 0.4 s after the user's un-maximize: on Virtual-1" "$(frame w)" '~' "@Virtual-1 "
sleep 1.5
echo "w 2 s after: $(frame w) | m2 $(frame m2)"
check "w 2 s after the un-maximize: on Virtual-1, not maximized" "$(frame w)" '~' "@Virtual-1 max=0$"
check "'Monocle window manually unmaximized' lines for w" "$(elog "Monocle window manually unmaximized.*$WU" | wc -l)" -eq 1
check "w floating" "$(floating "$WID")" = true
check "transfer lines for w since the un-maximize" "$(elog "Window moved between monitors: .*$WU" | wc -l)" -eq 0
finish
