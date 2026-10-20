#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: GPL-3.0-or-later
#
# f1035-deleted-layout-minimized: a deleted layout's minimized window follows the replacement
# layout's numbered zone. Runner: 1 output, no flags.
# Staging: LA, a duplicate of COLS3, is the default layout (defaultLayoutId) and Virtual-0's own
#   assignment is cleared, so the screen runs LA by default. Snap Assist off. w1 snapped into LA's
#   zone 2, then minimized. LayoutRegistry.deleteLayout LA (the screen falls back to the registry's
#   replacement, read after the delete), then w1 unminimized.
# Asserts: after the unminimize w1 holds the replacement's zone 2 and is not floating.
# A FAIL means: the layout guard refused the unfloat and w1 came back floating with no zone.
set -u
NEST="$1"
LOG="$2"
. "$(dirname "$0")/../lib.sh"
kwjs_ok
setting snapAssistEnabled false
LA=$(PZ org.plasmazones.LayoutRegistry.duplicateLayout "$COLS3")
echo "LA $LA"
case "$LA" in "{"*) ;; *) stage_fail "duplicateLayout failed ($LA)" ;; esac
setting defaultLayoutId "$LA"
PZ org.plasmazones.LayoutRegistry.clearAssignment Virtual-0 >/dev/null
sleep 1.5
# screenlayout -> the layout id Virtual-0 runs. getLayoutForScreen answers a bare id string (not JSON)
# and falls through to the default layout when the screen has no assignment of its own.
screenlayout() { PZ org.plasmazones.LayoutRegistry.getLayoutForScreen Virtual-0 | tr -d ' \n'; }
L=$(screenlayout)
echo "Virtual-0 layout: ${L:-empty} | own assignment '$(PZ org.plasmazones.LayoutRegistry.getAssignedLayoutForScreen Virtual-0 | tr -d '\n')'"
[ "$L" = "$LA" ] || stage_fail "Virtual-0 is not on LA"
LA2=$(zoneid "$LA" 2)
echo "LA zone 2 $LA2"
[ -n "$LA2" ] || stage_fail "no zone 2 id in LA"
open w1
ids
W1U=$(uuid w1)
[ -n "$W1U" ] || stage_fail "w1 never mapped"
W1="org.pz.plain|$W1U"
PZ org.plasmazones.Control.snapWindowToZone "$W1" 2 Virtual-0 >/dev/null
sleep 2
echo "w1 staged: $(geo w1) zone=$(zoneof "$W1")"
[ "$(zoneof "$W1")" = "$LA2" ] || stage_fail "w1 is not in LA's zone 2"
on w1 'w[i].minimized = true;'
sleep 2
echo "w1 minimized: $(geo w1) floating=$(floating "$W1")"
echo "== deleteLayout LA"
dmark
echo "  deleteLayout: '$(PZ org.plasmazones.LayoutRegistry.deleteLayout "$LA")'"
sleep 3
RL=$(screenlayout)
RL2=$(zoneid "$RL" 2)
echo "  Virtual-0 layout now (the replacement): ${RL:-empty}, its zone 2 ${RL2:-none}"
{ [ -n "$RL2" ] && [ "$RL" != "$LA" ]; } || stage_fail "no replacement layout with a zone 2 after the delete"
echo "  re-slot line: $(dlog "Re-slotted the pre-float zones of [0-9]* minimized window.*" | head -1)"
echo "== w1 unminimized"
on w1 'w[i].minimized = false;'
sleep 2.5
echo "  w1 $(geo w1)"
check "w1's zone after the unminimize (the replacement's zone 2)" "$(zoneof "$W1")" = "$RL2"
check "w1 floating after the unminimize" "$(floating "$W1")" = false
finish
