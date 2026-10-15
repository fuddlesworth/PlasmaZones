#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: GPL-3.0-or-later
#
# f973-strip-fullscreen-maximize: the offered-column centring leaves KWin's own fullscreen and
# maximize of a strip tile alone. Runner: 1 output, flag X.
# Staging: Virtual-0 scrolling with two Wayland tiles s1, s2 and one X11 tile x1.
#   Step 1: a KWin script sets s2.fullScreen = true, then back to false.
#   Step 2: a KWin script records every frame of s2 while it calls setMaximize(true, true); then
#   setMaximize(false, false). Step 3: the same on x1.
# Asserts: step 1 logs no "Asserting fullscreen area for strip tile" for s2 and s2 covers the
#   output; steps 2 and 3 record no frame at the final maximized size anywhere but the final
#   position; step 3 logs no "Countering external move of scroll-managed window" for x1.
# A FAIL means: the centring moved KWin's output-sized ack to the column origin (the assert
#   workaround fired, or a frame landed at the column origin), or x1 was countered back.
set -u
NEST="$1"
LOG="$2"
. "$(dirname "$0")/../lib.sh"
kwjs_ok
# jslong 'JS BODY' SECONDS: a KWin script left loaded for SECONDS, so its signal connections record
jslong() {
    local f="$NEST/f973-$BASHPID-$RANDOM.js"
    printf '%s\n' "$1" > "$f"
    qdbus6 org.kde.KWin /Scripting org.kde.kwin.Scripting.loadScript "$f" >/dev/null
    qdbus6 org.kde.KWin /Scripting org.kde.kwin.Scripting.start >/dev/null 2>&1
    sleep "$2"
    qdbus6 org.kde.KWin /Scripting org.kde.kwin.Scripting.unloadScript "$f" >/dev/null 2>&1
    rm -f "$f"
}
# FS: the extra field for frame, so each read also says whether the window is fullscreen
FS='" fs=" + w.fullScreen'
# record CAPTION 'JS ACTION': record CAPTION's frames for 1.5 s around the action and print how many
# frames sat at the final size somewhere other than the final position
record() {
    local c="$1" tag="F973F$RANDOM"
    jslong "var w = null; const ws = workspace.windowList(); for (var i = 0; i < ws.length; i++) { if (ws[i].caption === \"$c\") { w = ws[i]; } }
if (w) {
    var t0 = Date.now();
    var g0 = w.frameGeometry;
    print(\"$tag 0 \" + g0.x + \",\" + g0.y + \" \" + g0.width + \"x\" + g0.height + \" max=\" + w.maximizeMode + \" start\");
    w.frameGeometryChanged.connect(function () { var g = w.frameGeometry; print(\"$tag \" + (Date.now() - t0) + \" \" + g.x + \",\" + g.y + \" \" + g.width + \"x\" + g.height + \" max=\" + w.maximizeMode); });
    $2
}" 1.5
    grep "js: $tag " "$LOG" | sed "s/.*js: $tag //" > "$NEST/f973-frames.txt"
    sed 's/^/    /' "$NEST/f973-frames.txt" >&2
    awk 'NR > 1 { pos[NR] = $2; size[NR] = $3; n = NR } END { if (n < 2) { print "none"; exit } c = 0; for (i = 2; i < n; i++) if (size[i] == size[n] && pos[i] != pos[n]) c++; print c }' "$NEST/f973-frames.txt"
}
assign Virtual-0 "scrolling:"
sleep 2
for c in s1 s2; do
    open "$c"
done
xopen x1
sleep 0.5
ids
S2=$(uuid s2)
X1=$(uuid x1)
[ -n "$S2" ] && [ -n "$X1" ] || stage_fail "s2 or x1 missing"
read -r OX OY OW OH <<< "$(scr Virtual-0)"
echo "strip: s1 $(frame s1 "$FS") | s2 $(frame s2 "$FS") | x1 $(frame x1 "$FS")"

echo "== step 1: s2 fullscreen"
emark
on s2 'w[i].fullScreen = true;'
sleep 2
check "step 1: s2 fullscreen over the output" "$(frame s2 "$FS")" = "$OX,$OY ${OW}x$OH @Virtual-0 max=0 fs=true"
check "step 1: 'Asserting fullscreen area for strip tile' lines for s2" \
    "$(elog "Asserting fullscreen area for strip tile.*$S2" | wc -l)" -eq 0
on s2 'w[i].fullScreen = false;'
sleep 2.5
echo "  s2 after leaving fullscreen: $(frame s2 "$FS")"

echo "== step 2: s2 setMaximize(true, true), frames recorded"
N=$(record s2 'w.setMaximize(true, true);')
[ "$N" != none ] || stage_fail "no frame change recorded for s2's maximize"
check "step 2: s2 frames at the final size off the final position" "$N" -eq 0
echo "  s2 after: $(frame s2 "$FS")"
on s2 'w[i].setMaximize(false, false);'
sleep 2.5

echo "== step 3: x1 (X11) setMaximize(true, true), frames recorded"
emark
N=$(record x1 'w.setMaximize(true, true);')
[ "$N" != none ] || stage_fail "no frame change recorded for x1's maximize"
check "step 3: x1 frames at the final size off the final position" "$N" -eq 0
sleep 1
echo "  x1 after: $(frame x1 "$FS")"
check "step 3: 'Countering external move of scroll-managed window' lines for x1" \
    "$(elog "Countering external move of scroll-managed window.*$X1" | wc -l)" -eq 0
on x1 'w[i].setMaximize(false, false);'
sleep 2.5
finish
