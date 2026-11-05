#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: GPL-3.0-or-later
#
# f911-min-size-rereported: a declared minimum size is reported again each time the window is
# tiled again. Runner: 1 output, flags XS.
# Staging: Virtual-0 autotile:master-stack (two columns). o1 a Wayland plain_gtk, x1 an X11
#   grow_gtk that raises its minimum width to 1000 (more than half the output) 500 ms after
#   mapping, so the engine widens x1's column. x1 is then minimized and restored, and separately
#   floated and unfloated (toggle_window_float twice): the engines drop their stored minimum on
#   both.
# Asserts: after each re-tile the effect logs "Reporting min size for <x1>" again, the two
#   columns do not overlap, and after the float round trip x1 still fits on the output.
# A FAIL means: the change-gated cache saw an unchanged minimum and sent nothing, so the engine
#   tiled x1 at half width and KWin's X11 minimum pushed it over o1 or past the screen edge.
set -u
NEST="$1"
LOG="$2"
. "$(dirname "$0")/../lib.sh"
kwjs_ok
read -r VX _ VW _ <<< "$(scr Virtual-0)"
assign Virtual-0 "autotile:master-stack"
sleep 2
open o1 400 300
xclient grow_gtk.py x1 400 300 1000 300 500
sleep 2
ids
X1=$(uuid x1)
[ -n "$X1" ] || stage_fail "x1 never mapped"
# cols -> "X W MINW OVERLAP": x1's frame x, width, declared minimum width, and the overlap of the
# two columns on x (positive when they overlap)
cols() {
    jsread COLS '(function () { const w = workspace.windowList(); var a = null, b = null; for (var i = 0; i < w.length; i++) { if (w[i].caption === "x1") { a = w[i]; } if (w[i].caption === "o1") { b = w[i]; } } if (!a || !b) { return ""; } var l = a.frameGeometry.x < b.frameGeometry.x ? a : b; var r = l === a ? b : a; return Math.round(a.frameGeometry.x) + " " + Math.round(a.frameGeometry.width) + " " + Math.round(a.minSize.width) + " " + Math.round(l.frameGeometry.x + l.frameGeometry.width - r.frameGeometry.x); })()'
}
reports() { tail -n +"$1" "$LOG" | grep -c "Reporting min size for .*$X1"; }
# readcols: cols into C and its fields; an empty read is the KWin scripting flake or a lost window
readcols() {
    C=$(cols)
    [ -n "$C" ] || stage_fail "the column read printed nothing"
    read -r CX CW MINW OV <<< "$C"
    echo "  x1 (x w minW overlap): $C"
}
echo "x1 with its grown minimum:"
readcols
[ "$MINW" = 1000 ] || stage_fail "x1 did not grow its minimum to 1000 ($C)"

echo "== minimize and restore x1"
on x1 'w[i].minimized = true;'
sleep 1.5
MARK=$(wc -l < "$LOG")
on x1 'w[i].minimized = false; workspace.activeWindow = w[i];'
sleep 2.5
readcols
check "min size reports for x1 after the unminimize" "$(reports "$MARK")" -ge 1
check "column overlap after the unminimize" "$OV" -le 0

echo "== float and unfloat x1"
MARK=$(wc -l < "$LOG")
shortcut toggle_window_float
sleep 2
shortcut toggle_window_float
sleep 2.5
readcols
check "min size reports for x1 after the float round trip" "$(reports "$MARK")" -ge 1
check "column overlap after the float round trip" "$OV" -le 0
check "x1's right edge after the float round trip (the output ends at $((VX + VW)))" "$((CX + CW))" -le "$((VX + VW))"
finish
