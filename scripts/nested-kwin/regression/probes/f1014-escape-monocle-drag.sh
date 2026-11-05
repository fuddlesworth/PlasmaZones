#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: GPL-3.0-or-later
#
# f1014-escape-monocle-drag: Escape on a dragged Monocle tile leaves it in the stack (F1014), and
# the drag never reads as a manual un-maximize (F775). Runner: 1 output, flags F.
# Staging: Virtual-0 autotile:monocle with m2 then m1 (monocle maximizes from two tiles), m1 active
#   on top, both maximized. A Meta+left drag of m1 by 40 px is held about 2.5 s, then Escape is
#   pressed with the button still down (KWin cancels the move and re-applies its starting
#   maximize), then the button comes up.
# Asserts: no "Monocle window manually unmaximized" line for m1 mid-drag or after; the effect logs
#   "Window move finished without button release, reverted= true"; m1 and m2 are both maximized
#   again; m1 does not read floating; the drag ends with endDrag action= 4 (CancelSnap) and the
#   daemon gets no float send for m1.
# A FAIL means: the cancelled drag was taken as a drop (ApplyFloat) and m1 left the stack floating.
set -u
NEST="$1"
LOG="$2"
. "$(dirname "$0")/../lib.sh"
kwjs_ok
[ -x "$PZR_FAKEINPUT" ] || stage_fail "no fakeinput binary (the probe needs flag F; run.sh builds it)"
# monostate -> "m1 x,y wxh max=N move=B | m2 ..." for the two Monocle members
monostate() {
    jsread T14MST '(function () { var out = []; const w = workspace.windowList(); for (var i = 0; i < w.length; i++) { var c = w[i].caption; if (c === "m1" || c === "m2") { out.push(c + " " + w[i].frameGeometry.x + "," + w[i].frameGeometry.y + " " + w[i].frameGeometry.width + "x" + w[i].frameGeometry.height + " max=" + w[i].maximizeMode + " move=" + w[i].move); } } out.sort(); return out.join(" | "); })()'
}
assign Virtual-0 "autotile:monocle"
sleep 2
for c in m2 m1; do
    open "$c" 400 300
done
ids
M1=$(uuid m1)
[ -n "$M1" ] || stage_fail "m1 never mapped"
ID="org.pz.plain|$M1"
read -r SX SY SW SH <<< "$(scr Virtual-0)"
CX=$((SX + SW / 2))
CY=$((SY + SH / 2))
activate m1
S=$(monostate)
echo "staged: $S"
case "$S" in *"m1 "*"max=3"*"m2 "*"max=3"*) ;; *) stage_fail "no monocle ($S)" ;; esac
unmax() { elog "Monocle window manually unmaximized: [^ ]*$M1" | wc -l; }

echo "== drag m1 40 px, hold, Escape"
emark
dmark
# KEY_LEFTMETA = 125, KEY_ESC = 1.
fake abs "$CX" "$CY" wait 100 keydown 125 wait 50 down left wait 150 \
    path "$CX" "$CY" $((CX + 40)) "$CY" 8 30 wait 2500 keydown 1 wait 40 keyup 1 wait 300 up left wait 50 keyup 125 &
FIPID=$!
sleep 1.6
MID=$(monostate)
echo "mid-drag: $MID"
case "$MID" in *"m1 "*"move=true"*) ;; *) note "m1 does not read as moving mid-drag" ;; esac
check "manual un-maximize lines for m1 mid-drag" "$(unmax)" -eq 0
wait "$FIPID"
sleep 2.5
S=$(monostate)
echo "after Escape: $S"
check "the move-finished line" "$(elog "Window move finished without button release, reverted= [a-z]*" | head -1)" \
    = "Window move finished without button release, reverted= true"
check "m1 and m2 maximized after Escape" "$S" '~' "m1 [^|]* max=3 .*m2 [^|]* max=3 "
check "m1 floating after Escape" "$(floating "$ID")" = false
check "manual un-maximize lines for m1 since the drag" "$(unmax)" -eq 0
check "m1's endDrag action" "$(elog "endDrag outcome: [^ ]*$M1.*" | grep -o "action= [0-9]*" | head -1)" = "action= 4"
check "daemon float sends for m1" "$(floatsends "$M1")" -eq 0
finish
