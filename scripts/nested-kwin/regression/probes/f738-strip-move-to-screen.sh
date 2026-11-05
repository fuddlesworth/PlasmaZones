#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: GPL-3.0-or-later
#
# f738-strip-move-to-screen: KWin's own Move to Screen of a strip tile is a real move to the other
# monitor, for a Wayland and an X11 tile. Runner: 2 outputs, flags XF.
# Staging: Virtual-1 autotile:bsp holding u1. Virtual-0 scrolling with three Wayland tiles s1, s2,
#   s3 and one X11 tile x1; W is the Wayland tile lying wholly inside Virtual-0.
#   Step 1: workspace.sendClientToScreen(W, Virtual-1), then u1 is activated and fakeinput clicks
#   the middle of W's new frame. Step 2: the same send for x1. Step 3: Virtual-0's strip is
#   scrolled (+600, -600, +600 px).
# Asserts: for W and x1, one daemon "windowCrossedScreens ... from Virtual-0 to Virtual-1" and one
#   effect "Window moved between monitors ... Virtual-0 -> Virtual-1", no "Countering external move"
#   line, the window on Virtual-1 and listed in its tiles; the click activates W; the scrolls run
#   no transfer and no crossing notice.
# A FAIL means: KWin's send left W a Virtual-0 column (painted and hit-tested there), or x1 was
#   countered back to its column.
set -u
NEST="$1"
LOG="$2"
. "$(dirname "$0")/../lib.sh"
kwjs_ok
need_outputs 2
send1() { js "var o = null; for (var i = 0; i < workspace.screens.length; i++) { if (workspace.screens[i].name === \"$2\") { o = workspace.screens[i]; } } const w = workspace.windowList(); for (var j = 0; j < w.length; j++) { if (w[j].caption === \"$1\" && o && w[j].output !== o) { workspace.sendClientToScreen(w[j], o); } }"; }
assign Virtual-1 "autotile:bsp"
sleep 2
open u1
sendto u1 Virtual-1 || stage_fail "u1 not on Virtual-1 ($(geo u1))"
assign Virtual-0 "scrolling:"
sleep 2
for c in s1 s2 s3; do
    open "$c"
    [ "$(outof "$(geo "$c")")" = Virtual-0 ] || sendto "$c" Virtual-0 || stage_fail "$c not on Virtual-0"
done
xopen x1
sleep 0.5
[ "$(outof "$(geo x1)")" = Virtual-0 ] || sendto x1 Virtual-0 || stage_fail "x1 not on Virtual-0"
read -r V0X _ V0W _ <<< "$(scr Virtual-0)"
ids
W=""
for c in s3 s2 s1; do
    read -r x y w h _ <<< "$(geo "$c")"
    o=$(outof "$(geo "$c")")
    x=${x%.*}
    w=${w%.*}
    echo "strip $c: $x $y $w $h @$o"
    if [ -z "$W" ] && [ "$o" = Virtual-0 ] && [ "$x" -ge "$V0X" ] && [ $((x + w)) -le $((V0X + V0W)) ]; then
        W="$c"
    fi
done
echo "x1: $(geo x1)"
[ -n "$W" ] || stage_fail "no Wayland tile wholly inside Virtual-0"
WU=$(uuid "$W")
X1=$(uuid x1)
[ -n "$WU" ] && [ -n "$X1" ] || stage_fail "$W or x1 has no id"
echo "W = $W (...${WU: -8}); Virtual-1 tiles (u1): $(tilelist Virtual-1)"

moved() {
    local c="$1" u="$2"
    echo "  $c now: $(geo "$c") | u1 $(geo u1)"
    check "$c: daemon crossing notices from Virtual-0 to Virtual-1" \
        "$(dlog "windowCrossedScreens: .*$u" | grep -c "from.*Virtual-0.*to.*Virtual-1")" -eq 1
    check "$c: effect transfer lines Virtual-0 -> Virtual-1" \
        "$(elog "Window moved between monitors: .*$u" | grep -c "Virtual-0.*->.*Virtual-1")" -eq 1
    check "$c: 'Countering external move' lines" \
        "$(elog "Countering external move of scroll-managed window.*$u" | wc -l)" -eq 0
    check "$c: on Virtual-1" "$(outof "$(geo "$c")")" = Virtual-1
    check "$c: listed in Virtual-1's tiles" "$(tilelist Virtual-1)" '~' "${u: -8}"
}

echo "== step 1: KWin sends $W to Virtual-1"
emark
dmark
send1 "$W" Virtual-1
sleep 3
moved "$W" "$WU"
activate u1
read -r x y w h _ <<< "$(geo "$W")"
CX=$((${x%.*} + ${w%.*} / 2))
CY=$((${y%.*} + ${h%.*} / 2))
fake abs "$CX" "$CY" wait 150 down left wait 80 up left wait 100
sleep 1
check "the click at $CX,$CY activates $W" "$(activecap)" = "$W"

echo "== step 2: KWin sends x1 to Virtual-1"
emark
dmark
send1 x1 Virtual-1
sleep 3
moved x1 "$X1"

echo "== step 3: scroll Virtual-0's strip"
emark
dmark
for px in 600 -600 600; do
    PZ org.plasmazones.Scrolling.scrollViewByPx Virtual-0 "$px" >/dev/null
    sleep 1.5
done
check "transfer lines during the scrolls" "$(elog "Window moved between monitors:" | wc -l)" -eq 0
check "daemon crossing notices during the scrolls" "$(dlog "windowCrossedScreens:" | wc -l)" -eq 0
finish
