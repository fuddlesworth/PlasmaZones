#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: GPL-3.0-or-later
#
# f685-evacuee-monocle-unmaximize: an evacuee's Monocle claim survives its output's return, so a
# user un-maximize afterwards floats it. Runner: 2 outputs, no flags.
# Staging: Virtual-1 autotile:monocle with two tiles, m2 then the subject m1 (the engine flags
#   monocle only from two tiles), m1 monocle-maximized there. Virtual-1 is disabled with
#   kscreen-doctor (m1 and m2 evacuate to Virtual-0 keeping their maximize) and enabled again
#   (KWin returns them and the daemon re-seats them, both maximized again). Then a user
#   setMaximize(false, false) on m1, then Tiling.retile Virtual-1.
# Asserts: the effect logs "Monocle window manually unmaximized: <m1>", isWindowFloating m1 is
#   true, and m1 stays unmaximized through the retile.
# A FAIL means: the evacuation dropped m1's claim, so the un-maximize read as a stray change and
#   the retile maximized m1 again while it stayed tiled.
set -u
NEST="$1"
LOG="$2"
. "$(dirname "$0")/../lib.sh"
kwjs_ok
need_outputs 2
on_exit 'ksd output.Virtual-1.enable >/dev/null'
assign Virtual-1 "autotile:monocle"
sleep 2
for c in m2 m1; do
    open "$c" 400 300
    sendto "$c" Virtual-1 || stage_fail "$c did not reach Virtual-1 ($(geo "$c"))"
    sleep 1
done
ids
M1=$(uuid m1)
[ -n "$M1" ] || stage_fail "m1 never mapped"
MID="org.pz.plain|$M1"
G=$(geo m1)
echo "m1 $G | m2 $(geo m2)"
{ [ "$(outof "$G")" = Virtual-1 ] && [ "$(maxof "$G")" = 3 ]; } || stage_fail "m1 is not monocle-maximized on Virtual-1 ($G)"
echo "== Virtual-1 disabled"
ksd output.Virtual-1.disable
sleep 4
G=$(geo m1)
echo "  evacuated: m1 $G | m2 $(geo m2)"
[ "$(outof "$G")" = Virtual-0 ] || stage_fail "m1 was not evacuated to Virtual-0 ($G)"
echo "== Virtual-1 enabled"
ksd output.Virtual-1.enable
sleep 5
G=$(geo m1)
echo "  returned: m1 $G | m2 $(geo m2)"
{ [ "$(outof "$G")" = Virtual-1 ] && [ "$(maxof "$G")" = 3 ]; } \
    || stage_fail "m1 did not come back monocle-maximized on Virtual-1 ($G); the return lost the maximize, which f1013's probe covers"
emark
on m1 'w[i].setMaximize(false, false);'
sleep 1.5
echo "  after the user un-maximize: m1 $(geo m1)"
check "'Monocle window manually unmaximized' lines for m1" "$(elog "Monocle window manually unmaximized.*$M1" | wc -l)" -eq 1
check "isWindowFloating m1" "$(floating "$MID")" = true
PZ org.plasmazones.Tiling.retile Virtual-1 >/dev/null
sleep 2.5
G=$(geo m1)
echo "  after Tiling.retile Virtual-1: m1 $G | m2 $(geo m2)"
check "m1's maximize mode after the retile" "$(maxof "$G")" = 0
finish
