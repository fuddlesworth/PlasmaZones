#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: GPL-3.0-or-later
#
# f1042-autotile-off-no-resnap: switching tiling off with no screen tiled resnaps nothing.
# Runner: 1 output, no flags.
# Staging: Virtual-0 COLS3 (no screen on autotile or scrolling), Snap Assist off, autotileEnabled
#   true. s1 and s2 snapped into zones 1 and 2, then s2 moved 15,10 px off its zone by script (a drift
#   a resnap would undo; s2 still holds its zone). Then autotileEnabled false.
# Asserts: s2 stays at its drifted frame and is not put back on its zone rect.
# A FAIL means: the toggle re-applied every snapping screen's zones.
set -u
NEST="$1"
LOG="$2"
. "$(dirname "$0")/../lib.sh"
kwjs_ok
on_exit 'setting autotileEnabled true'
setting snapAssistEnabled false
setting autotileEnabled true
assign Virtual-0 "$COLS3"
sleep 1.5
[ "$(PZ org.plasmazones.Settings.getSetting autotileEnabled)" = true ] || stage_fail "autotileEnabled did not turn on"
open s1
open s2
ids
S1="org.pz.plain|$(uuid s1)"
S2="org.pz.plain|$(uuid s2)"
PZ org.plasmazones.Control.snapWindowToZone "$S1" 1 Virtual-0 >/dev/null
PZ org.plasmazones.Control.snapWindowToZone "$S2" 2 Virtual-0 >/dev/null
sleep 2
GZ=$(geo s2)
echo "staged: s1 $(geo s1) zone=$(zoneof "$S1") | s2 $GZ zone=$(zoneof "$S2")"
case "$(zoneof "$S2")" in "{"*) ;; *) stage_fail "s2 did not snap" ;; esac
read -r X Y _ _ <<< "$GZ"
moveto s2 $((${X%.*} + 15)) $((${Y%.*} + 10))
sleep 2
GD=$(geo s2)
echo "s2 drifted: $GD zone=$(zoneof "$S2")"
case "$(zoneof "$S2")" in "{"*) ;; *) stage_fail "the scripted drift unsnapped s2, so no resnap can show" ;; esac
[ "$(rect "$GD")" != "$(rect "$GZ")" ] || stage_fail "the scripted drift did not move s2"
echo "== autotileEnabled false"
emark
dmark
setting autotileEnabled false
sleep 3
GA=$(geo s2)
echo "  s2 $GA | s1 $(geo s1)"
check "s2 still at its drifted frame" "$(rect "$GA")" = "$(rect "$GD")"
check "s2 not back on its zone rect" "$(rect "$GA")" != "$(rect "$GZ")"
finish
