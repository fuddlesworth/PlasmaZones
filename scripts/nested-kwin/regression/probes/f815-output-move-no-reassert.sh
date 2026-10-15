#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: GPL-3.0-or-later
#
# f815-output-move-no-reassert: the strip does not re-assert old coordinates after its output
# moves. Runner: 2 outputs, flags XS.
# Staging: Virtual-0 scrolling with three non-resizable Wayland clients s1..s3 (fixed_gtk.py,
#   320x260, so the offered-column re-assert centres them in wider columns) and one X11 client x1
#   (plain_gtk.py), which the counter-assert covers. A persistent KWin script logs every frame
#   change of the four with a millisecond stamp; then kscreen-doctor swaps the outputs' positions
#   (Virtual-0 moves right).
# Asserts: no "Countering external move of scroll-managed window" after the swap, and no frame in
#   any window's trail lands left of Virtual-0's new origin (each goes old to new, never back).
# A FAIL means: the strip countered KWin's translate for x1 and re-asserted the old coordinates
#   before the daemon's geometry pass moved the windows again.
set -u
NEST="$1"
LOG="$2"
. "$(dirname "$0")/../lib.sh"
kwjs_ok
need_outputs 2
S0="$SCR"
read -r _ _ V1W _ <<< "$(scr Virtual-1)"
ARM="$NEST/pzr-arm-f815.js"
disarm() {
    qdbus6 org.kde.KWin /Scripting org.kde.kwin.Scripting.unloadScript "$ARM" >/dev/null 2>&1
    rm -f "$ARM"
}
on_exit disarm
assign Virtual-0 "scrolling:"
sleep 2
for c in s1 s2 s3; do
    client fixed_gtk.py "$c" 320 260
    sendto "$c" Virtual-0 || stage_fail "$c did not reach Virtual-0 ($(geo "$c"))"
done
xopen x1 400 300
sendto x1 Virtual-0 || stage_fail "x1 did not reach Virtual-0 ($(geo x1))"
sleep 2
ids
X1=$(uuid x1)
[ -n "$X1" ] || stage_fail "x1 never mapped"
FRAMES='(function () { const w = workspace.windowList(); var out = []; for (var i = 0; i < w.length; i++) { if (/^(s[1-3]|x1)$/.test(w[i].caption)) { out.push(w[i].caption + "=" + w[i].frameGeometry.x + "," + w[i].frameGeometry.y + "," + w[i].frameGeometry.width + "x" + w[i].frameGeometry.height); } } out.sort(); return out.join(" "); })()'
echo "strip before the move: $(jsread FRAMES "$FRAMES")"
emark
printf '%s\n' 'const w = workspace.windowList(); for (var i = 0; i < w.length; i++) { if (/^(s[1-3]|x1)$/.test(w[i].caption)) { (function (win) { win.frameGeometryChanged.connect(function () { print("T15B " + Date.now() + " " + win.caption + " " + win.frameGeometry.x + "," + win.frameGeometry.y + " " + win.frameGeometry.width + "x" + win.frameGeometry.height); }); })(w[i]); } } print("T15B " + Date.now() + " armed");' > "$ARM"
qdbus6 org.kde.KWin /Scripting org.kde.kwin.Scripting.loadScript "$ARM" >/dev/null
qdbus6 org.kde.KWin /Scripting org.kde.kwin.Scripting.start >/dev/null 2>&1
sleep 0.8
elog "js: T15B [0-9]* armed" | grep -q . || stage_fail "the frame logger script did not start"
WAYLAND_DISPLAY="$WAYLAND_DISPLAY" kscreen-doctor "output.Virtual-1.position.0,0" "output.Virtual-0.position.$V1W,0" \
    > "$NEST/pzr-f815-ksd.out" 2>&1
KSD=$?
KT=$(date +%s%3N)
sleep 4
disarm
S1=$(screens)
echo "kscreen-doctor swap exit $KSD ($(head -c 160 "$NEST/pzr-f815-ksd.out" | tr '\n' ' '))"
echo "outputs after the swap: $S1"
[ "$S1" != "$S0" ] || stage_fail "the outputs did not move"
SCR="$S1"
read -r NX _ <<< "$(scr Virtual-0)"
echo "strip after the move and the daemon's pass: $(jsread FRAMES "$FRAMES")"
elog "Countering external move" | head -3 | sed 's/^/    /'
check "counter lines since the swap" "$(elog "Countering external move of scroll-managed window" | wc -l)" -eq 0
for c in s1 s2 s3 x1; do
    trail=$(elog "js: T15B [0-9]* $c .*" | awk -v kt="$KT" '{ printf "%+d:%s ", $3 - kt, $5 }')
    echo "  $c trail (ms since kscreen-doctor returned): ${trail:-no change}"
    check "$c trail frames left of Virtual-0's new origin x=$NX" \
        "$(elog "js: T15B [0-9]* $c .*" | awk -v nx="$NX" '{ split($5, p, ","); if (p[1] + 0 < nx + 0) n++ } END { print n + 0 }')" -eq 0
done
finish
