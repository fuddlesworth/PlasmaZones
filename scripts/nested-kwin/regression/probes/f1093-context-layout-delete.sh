#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: GPL-3.0-or-later
#
# f1093-context-layout-delete: deleting the layout assigned to the monitor floats its snapped
# windows back to their own spots. Runner: 1 output, no flags.
# Staging: DUP, a duplicate of COLS3, assigned to Virtual-0 directly, Snap Assist off. p1 placed
#   free at R (150,120 520x360) then snapped into zone 2, so its float-back is R. p2 opened where KWin
#   places it (its own spot Q) and snapped into zone 1. LayoutRegistry.deleteLayout DUP (the registry
#   purges the assignment to none).
# Asserts: p1 is back at R and floating with no zone; p2 is back at Q and floating with no zone.
# A FAIL means: the delete unsnapped the windows where they stood, at their zone rects.
set -u
NEST="$1"
LOG="$2"
. "$(dirname "$0")/../lib.sh"
kwjs_ok
setting snapAssistEnabled false
DUP=$(PZ org.plasmazones.LayoutRegistry.duplicateLayout "$COLS3")
echo "DUP: $DUP"
case "$DUP" in "{"*) ;; *) stage_fail "duplicateLayout answered '$DUP'" ;; esac
assign Virtual-0 "$DUP"
sleep 1.5
[ "$(PZ org.plasmazones.LayoutRegistry.getLayoutForScreen Virtual-0 | tr -d ' \n')" = "$DUP" ] || stage_fail "Virtual-0 is not on DUP"
open p1
on p1 'w[i].frameGeometry = {x: 150, y: 120, width: 520, height: 360};'
sleep 1.5
open p2
ids
P1="org.pz.plain|$(uuid p1)"
P2="org.pz.plain|$(uuid p2)"
R=$(geo p1)
Q=$(geo p2)
echo "p1 free at R: $R | p2 free at Q: $Q"
[ "$(rect "$R")" = "150 120 520 360" ] || stage_fail "p1 did not take the scripted frame ($R)"
PZ org.plasmazones.Control.snapWindowToZone "$P1" 2 Virtual-0 >/dev/null
PZ org.plasmazones.Control.snapWindowToZone "$P2" 1 Virtual-0 >/dev/null
sleep 2
echo "staged: p1 $(geo p1) zone=$(zoneof "$P1") | p2 $(geo p2) zone=$(zoneof "$P2")"
case "$(zoneof "$P1") $(zoneof "$P2")" in "{"*" {"*) ;; *) stage_fail "p1 and p2 are not both snapped" ;; esac
echo "== deleteLayout DUP"
dmark
echo "  deleteLayout: '$(PZ org.plasmazones.LayoutRegistry.deleteLayout "$DUP")'"
sleep 3
A1=$(geo p1)
A2=$(geo p2)
echo "  resnap buffer line: $(dlog "Resnap buffer \(removed layout\).*" | head -1)"
echo "  p1 $A1 | p2 $A2"
check "p1 back at R" "$(rect "$A1")" = "$(rect "$R")"
check "p1 floating" "$(floating "$P1")" = true
check "p1's zone" "$(zoneof "$P1")" = ""
check "p2 back at Q" "$(rect "$A2")" = "$(rect "$Q")"
check "p2 floating" "$(floating "$P2")" = true
check "p2's zone" "$(zoneof "$P2")" = ""
finish
