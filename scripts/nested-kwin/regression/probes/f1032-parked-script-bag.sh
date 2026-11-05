#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: GPL-3.0-or-later
#
# f1032-parked-script-bag: the parked algorithm script bag comes back into the live state when the
# monitor returns. Runner: 2 outputs, flag F (fakeinput for the interactive resize).
# Staging: Virtual-1 autotile:aligned-grid (its onWindowResized keeps the column fractions in the
#   script bag), Virtual-0 COLS3, Snap Assist off. g1 and g2 tiled on Virtual-1 in uniform columns,
#   then the left tile's right edge dragged 150 px right with KWin's Meta+right-button resize, so the
#   bag holds non-uniform fractions. Virtual-1 is disabled and enabled again (KWin returns g1 and g2,
#   AutotileEngine::readoptParked re-seats them).
# Asserts: g1 and g2 come back at the tuned frames, not the uniform ones.
# A FAIL means: the bag went only into the stash, which the existing state never reads.
set -u
NEST="$1"
LOG="$2"
. "$(dirname "$0")/../lib.sh"
kwjs_ok
need_outputs 2
on_exit 'ksd output.Virtual-1.enable >/dev/null'
setting snapAssistEnabled false
assign Virtual-0 "$COLS3"
assign Virtual-1 "autotile:aligned-grid"
sleep 2
for c in g1 g2; do
    open "$c"
    sendto "$c" Virtual-1 || stage_fail "$c not on Virtual-1 ($(geo "$c"))"
done
sleep 2
U1=$(geo g1)
U2=$(geo g2)
echo "uniform: g1 $U1 | g2 $U2 | tiles $(tiles Virtual-1)"
case "$(tiles Virtual-1)" in 2*) ;; *) stage_fail "Virtual-1 does not hold two tiles" ;; esac
# the left tile is resized from inside its right edge
read -r X1 _ _ _ <<< "$U1"
read -r X2 _ _ _ <<< "$U2"
L=g1
[ "${X2%.*}" -lt "${X1%.*}" ] && L=g2
read -r LX LY LW LH _ <<< "$(geo "$L")"
EX=$((${LX%.*} + ${LW%.*} - 25))
EY=$((${LY%.*} + ${LH%.*} / 2))
activate "$L"
fake abs "$EX" "$EY" wait 100 keydown 125 wait 50 down right wait 150 \
    path "$EX" "$EY" $((EX + 150)) "$EY" 10 30 wait 300 up right wait 50 keyup 125
sleep 2.5
R1=$(geo g1)
R2=$(geo g2)
echo "tuned (the $L edge dragged): g1 $R1 | g2 $R2"
[ "$(rect "$R1") $(rect "$R2")" != "$(rect "$U1") $(rect "$U2")" ] || stage_fail "the resize left the grid uniform (no bag to park)"
echo "== Virtual-1 disabled"
dmark
ksd output.Virtual-1.disable
sleep 4
echo "  park: $(dlog "AutotileEngine::parkOutput: .*" | head -1)"
echo "== Virtual-1 enabled again"
dmark
ksd output.Virtual-1.enable
sleep 5
A1=$(geo g1)
A2=$(geo g2)
echo "  readopt lines: $(dlog "AutotileEngine::readoptParked: .*" | wc -l)"
echo "  g1 $A1 | g2 $A2"
check "g1 and g2 back at the tuned frames" "$(rect "$A1") | $(rect "$A2")" = "$(rect "$R1") | $(rect "$R2")"
check "g1 and g2 not back at the uniform frames" "$(rect "$A1") | $(rect "$A2")" != "$(rect "$U1") | $(rect "$U2")"
finish
