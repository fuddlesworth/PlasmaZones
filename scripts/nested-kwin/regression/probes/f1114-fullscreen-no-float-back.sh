#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: GPL-3.0-or-later
#
# f1114-fullscreen-no-float-back: a strip tile entering its own fullscreen pushes no column rect as
# its float-back. Runner: 1 output, no flags, PZ_F1114_CLIENT=plain
# (f1114-fullscreen-no-float-back-plain) or fixed (f1114-fullscreen-no-float-back-fixed).
# Staging: Virtual-0 "scrolling:", Snap Assist off. c1 and t open as columns (t: plain_gtk.py, or
#   fixed_gtk.py 500x400, which commits its own size so its frame misses the column rect). t is
#   floated, moved to a free spot and unfloated back into its column. Then t.fullScreen = true, then
#   false (KWin scripting), then t is floated with the float shortcut. A dbus-monitor watches the
#   effect's WindowTracking.storePreTileGeometry and Scrolling.setWindowFullscreenFloat calls.
# Asserts: the fullscreen edge sends setWindowFullscreenFloat for t (the staging) and no
#   storePreTileGeometry naming t across the fullscreen on and off. Where the float lands is
#   printed, not asserted.
# A FAIL means: KWin's fullscreen restore rect (the column) was pushed as t's float-back.
set -u
NEST="$1"
LOG="$2"
. "$(dirname "$0")/../lib.sh"
CLIENT="${PZ_F1114_CLIENT:-plain}"
kwjs_ok
read -r BX BY _ _ <<< "$(scr Virtual-0)"
setting snapAssistEnabled false
assign Virtual-0 "scrolling:"
sleep 2
open c1
case "$CLIENT" in
plain) open t ;;
fixed) client fixed_gtk.py t 500 400 ;;
*) stage_fail "unknown PZ_F1114_CLIENT '$CLIENT'" ;;
esac
sleep 1.5
ids
TU=$(uuid t)
[ -n "$TU" ] || stage_fail "t never mapped"
TW="org.pz.plain|$TU"
echo "strip: $(striporder Virtual-0) | t $(geo t)"
activate t
shortcut toggle_window_float
sleep 2
moveto t $((BX + 40)) $((BY + 40))
sleep 1.5
S=$(geo t)
echo "t floated and moved: $S floating=$(floating "$TW")"
[ "$(floating "$TW")" = true ] || stage_fail "t did not float"
activate t
shortcut toggle_window_float
sleep 2
C=$(geo t)
echo "t back in its column: $C floating=$(floating "$TW") | strip $(striporder Virtual-0)"
[ "$(floating "$TW")" = false ] || stage_fail "t did not go back into the strip"
MON="$NEST/f1114-monitor.log"
mon_start "$MON" "type='method_call',interface='org.plasmazones.WindowTracking',member='storePreTileGeometry'" \
    "type='method_call',interface='org.plasmazones.Scrolling',member='setWindowFullscreenFloat'"
echo "== t fullscreen on"
mmark "$MON"
on t 'w[i].fullScreen = true;'
sleep 2.5
echo "  t $(geo t)"
echo "== t fullscreen off"
on t 'w[i].fullScreen = false;'
sleep 2.5
echo "  t $(geo t)"
msgs "$MON" | grep "$TU" | sed 's/org.pz.plain|[^ ]*/<t>/; s/^/    /'
[ "$(named "$MON" setWindowFullscreenFloat "$TU")" -ge 1 ] || stage_fail "no setWindowFullscreenFloat for t; the fullscreen hold is not exercised"
check "storePreTileGeometry calls naming t across the fullscreen edge" "$(named "$MON" storePreTileGeometry "$TU")" -eq 0
echo "== t floated"
activate t
shortcut toggle_window_float
sleep 2.5
F=$(geo t)
echo "  t $F floating=$(floating "$TW")"
note "t floats at $(rect "$F") (free spot $(rect "$S"), column $(rect "$C"))"
finish
