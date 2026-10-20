#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: GPL-3.0-or-later
#
# f1029-parked-overflow-float: an overflow float parked with its output comes back as overflow, not
# as a user float. Runner: 2 outputs, no flags.
# Staging: Virtual-0 and Virtual-1 autotile:bsp, autotileMaxWindows 2, autotileOverflowBehavior 0
#   (Float). t1, t2, t3 opened on Virtual-1: t3 floats as overflow. Virtual-1 is disabled (the
#   autotile engine parks the context) and enabled again (KWin returns the windows), then
#   autotileMaxWindows is raised to 3.
# Asserts: after the cap is raised t3 is not floating and Virtual-1 lists three tiles with t3 tiled.
# A FAIL means: t3 came back as a user float, which overflow recovery never returns to the layout.
set -u
NEST="$1"
LOG="$2"
. "$(dirname "$0")/../lib.sh"
kwjs_ok
need_outputs 2
ORIGMAX=$(PZ org.plasmazones.Settings.getSetting autotileMaxWindows)
on_exit 'ksd output.Virtual-1.enable >/dev/null; setting autotileMaxWindows "$ORIGMAX"'
setting autotileOverflowBehavior 0
setting autotileMaxWindows 2
assign Virtual-0 "autotile:bsp"
assign Virtual-1 "autotile:bsp"
sleep 2
for c in t1 t2 t3; do
    open "$c"
    sendto "$c" Virtual-1 || stage_fail "$c not on Virtual-1 ($(geo "$c"))"
done
sleep 2
ids
T3U=$(uuid t3)
[ -n "$T3U" ] || stage_fail "t3 never mapped"
T3="org.pz.plain|$T3U"
echo "staged: t1 $(geo t1) | t2 $(geo t2) | t3 $(geo t3) floating=$(floating "$T3") | tiles $(tiles Virtual-1)"
case "$(tiles Virtual-1)" in 2*) ;; *) stage_fail "Virtual-1 does not hold exactly two tiles" ;; esac
[ "$(floating "$T3")" = true ] || stage_fail "t3 is not floating as overflow"
echo "== Virtual-1 disabled"
dmark
ksd output.Virtual-1.disable
sleep 4
[ -n "$(dlog "AutotileEngine::parkOutput: .*")" ] || stage_fail "the autotile engine parked nothing for Virtual-1"
echo "== Virtual-1 enabled again"
dmark
ksd output.Virtual-1.enable
sleep 5
echo "  t3 $(geo t3) floating=$(floating "$T3") | tiles $(tiles Virtual-1)"
[ "$(dlog "AutotileEngine::readoptParked: .*" | wc -l)" -ge 1 ] || stage_fail "the autotile engine re-seated nothing on the return"
echo "== autotileMaxWindows 3"
setting autotileMaxWindows 3
sleep 3
echo "  t1 $(geo t1) | t2 $(geo t2) | t3 $(geo t3)"
check "t3 floating after the cap is raised" "$(floating "$T3")" = false
check "Virtual-1 tiles, t3 among them not floating" "$(tiles Virtual-1)" '~' "^3 .*'${T3U: -8}'"
finish
