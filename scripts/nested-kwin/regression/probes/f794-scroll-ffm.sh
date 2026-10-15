#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: GPL-3.0-or-later
#
# f794-scroll-ffm: scrolling focus-follows-mouse follows the setting after the effect's own mirror
# of it went. A smoke check of the change, with no defect run against it. Runner: 1 output,
# flags FS.
# Staging: Virtual-0 scrolling, three columns c1 c2 c3 (c1 and c3 visible side by side, c3 active).
#   The pointer hovers over the left column (x 200) and the right one (x 1300) in turn.
# Asserts: with scrollingFocusFollowsMouse on, each hover focuses the column under the pointer;
#   with it off, no hover changes the active window.
set -u
NEST="$1"
LOG="$2"
. "$(dirname "$0")/../lib.sh"
kwjs_ok
for c in c1 c2 c3; do
    open "$c" 400 300
done
assign Virtual-0 "scrolling:"
sleep 4
STATE='(function () { const w = workspace.windowList(); var out = []; for (var i = 0; i < w.length; i++) { var c = w[i].caption; if (/^c[0-9]$/.test(c)) { out.push(c + " " + w[i].frameGeometry.x + "," + w[i].frameGeometry.y + " " + w[i].frameGeometry.width); } } out.sort(); return out.join(" | "); })()'
echo "seated: $(jsread STATE "$STATE") active $(activecap)"
# colat X -> the caption of the column whose frame spans x = X on the top row (y 450)
colat() {
    jsread COLAT "(function () { const w = workspace.windowList(); for (var i = 0; i < w.length; i++) { var g = w[i].frameGeometry; if (/^c[0-9]\$/.test(w[i].caption) && g.x <= $1 && $1 < g.x + g.width && g.y <= 450 && 450 < g.y + g.height) { return w[i].caption; } } return \"(none)\"; })()"
}
LEFT=$(colat 200)
RIGHT=$(colat 1300)
echo "columns under the hover points: x 200 $LEFT, x 1300 $RIGHT"
case "$LEFT/$RIGHT" in
c[0-9]/c[0-9]) [ "$LEFT" != "$RIGHT" ] || stage_fail "one column spans both hover points" ;;
*) stage_fail "no column under a hover point (x 200 $LEFT, x 1300 $RIGHT)" ;;
esac
hover() {
    fake abs "$1" 450 wait 80 abs $(($1 + 15)) 460 wait 80 abs $(($1 + 30)) 450
    sleep 1.2
}
setting scrollingFocusFollowsMouse true
sleep 2
for x in 200 1300 200 1300; do
    hover "$x"
    want=$LEFT
    [ "$x" = 1300 ] && want=$RIGHT
    check "ffm on: active after hovering x $x" "$(activecap)" = "$want"
done
setting scrollingFocusFollowsMouse false
sleep 2
HELD=$(activecap)
for x in 200 1300 200; do
    hover "$x"
    check "ffm off: active after hovering x $x" "$(activecap)" = "$HELD"
done
finish
