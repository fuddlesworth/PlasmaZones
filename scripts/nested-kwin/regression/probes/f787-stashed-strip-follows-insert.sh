#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: GPL-3.0-or-later
#
# f787-stashed-strip-follows-insert: a strip stashed by a per-desktop mode switch, and the
# assignment that stashed it, follow their desktop through a desktop insert. Runner: 1 output,
# no flags.
# Staging: three desktops, scrolling on all (the screen default). Desktop 2 has b1, b2 (b2
#   widened to 0.8). Desktop 2 is assigned COLS3 (snapping), which stashes its strip. KWin
#   inserts a desktop at the front (old 2 becomes 3), and desktop 3 is assigned scrolling again.
# Asserts: after the insert desktop 3 snaps and desktop 2 scrolls; desktop 3 shows no strip
#   while it snaps; switched back to scrolling it rebuilds [b1][b2 wide].
# A FAIL means: the assignment stayed on desktop number 2, so desktop 3 resolved scrolling at
#   once and b1/b2 came back without their stashed shape.
set -u
NEST="$1"
LOG="$2"
. "$(dirname "$0")/../lib.sh"
kwjs_ok
for n in 1 2; do
    newdesk "$n" "D$((n + 1))"
    sleep 0.5
done
setting snapAssistEnabled false
assign Virtual-0 "scrolling:"
sleep 2
desk 2
open b1
open b2
activate b2
sleep 0.4
PZ org.plasmazones.Scrolling.setColumnWidthProportion Virtual-0 0.8 >/dev/null
sleep 1.5
ids
S=$(stripx)
echo "desktop 2 scrolling: $S"
D=$(echo "$S" | grep -o "\[b1 [0-9]*\]" | tr -d '[]' | awk '{ print $2 }')
W=$(echo "$S" | grep -o "\[b2 [0-9]*\]" | tr -d '[]' | awk '{ print $2 }')
[ -n "$D" ] && [ -n "$W" ] && [ "$S" = "[b1 $D] [b2 $W]" ] && [ "$W" -gt "$D" ] \
    || stage_fail "desktop 2's strip is not [b1][b2 wide] ($S)"
assignd Virtual-0 2 "$COLS3"
sleep 3
echo "desktop 2 switched to snapping: modes 1=$(modeof Virtual-0 1) 2=$(modeof Virtual-0 2) 3=$(modeof Virtual-0 3)"
[ "$(modeof Virtual-0 2)" = 0 ] || stage_fail "desktop 2 does not snap after its COLS3 assignment"
desk 1
qdbus6 org.kde.KWin /VirtualDesktopManager org.kde.KWin.VirtualDesktopManager.createDesktop 0 X >/dev/null
sleep 3
K=$(jsread P23B '(function () { const w = workspace.windowList(); var o = []; for (var i = 0; i < w.length; i++) { if (/^b[0-9]$/.test(w[i].caption)) { o.push(w[i].caption + "=" + w[i].desktops.map(function (d) { return d.x11DesktopNumber; })); } } o.sort(); return o.join(" "); })()')
echo "after the front insert: KWin $K"
[ "$K" = "b1=3 b2=3" ] || stage_fail "KWin did not move b1/b2 to desktop 3 ($K)"
check "desktop 3's mode after the insert (0 = snapping)" "$(modeof Virtual-0 3)" = 0
check "desktop 2's mode after the insert (2 = scrolling)" "$(modeof Virtual-0 2)" = 2
desk 3
check "desktop 3's strip while it snaps" "$(stripx)" = "(no strip)"
assignd Virtual-0 3 "scrolling:"
sleep 3.5
check "desktop 3's strip back in scrolling" "$(stripx)" = "[b1 $D] [b2 $W]"
finish
