#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: GPL-3.0-or-later
#
# f989-strip-maximize-legs: a strip tile's maximize and restore the interception claims animate
# from where the press left the window, Wayland and X11. Runner: 1 output, flags X.
# Staging: Virtual-0 scrolling, two strip tiles: c1 (Wayland) and x1 (X11 on the nested
#   Xwayland). Two rules by title stretch placeIn to 1300 ms and placeOut to 1700 ms (both under
#   the 2000 ms ceiling every rule duration is clamped to), so a leg's duration names its profile.
#   Each press is KWin's own setMaximize from a KWin script, the path a title-bar button takes:
#   the window is activated and its column frame read (PRE), then maximized (MAX read 1.2 s later)
#   and restored (REST read 1.2 s later).
# Asserts, per window: one "Maximize interception: claiming" per press; a maximize leg toward MAX
#   lasting 1300 that departs from PRE backed out by the batch's view delta (x1 is the second
#   column, so its maximize slides the view) and not from KWin's maximize area; a restore leg
#   toward REST lasting 1700 that departs from MAX.
# A FAIL means: the maximize logged "already at target geometry, skipping" with no leg, or the
#   restore rode placeIn from the column rect.
set -u
NEST="$1"
LOG="$2"
. "$(dirname "$0")/../lib.sh"
kwjs_ok
PIN=1300
POUT=1700
area() { wprop "$1" '(function () { var a; try { a = workspace.clientArea(KWin.MaximizeArea, w); } catch (e) { a = w.output.geometry; } return a.x + " " + a.y + " " + a.width + " " + a.height; })()'; }
setmax() { kwjs "const w = workspace.windowList(); for (var i = 0; i < w.length; i++) { if (w[i].caption === \"$1\") { w[i].setMaximize($2, $2); } }"; }
near() {
    awk -v a="$1" -v b="$2" 'BEGIN { n = split(a, x, " "); m = split(b, y, " "); ok = (n >= 4 && m >= 4);
        for (i = 1; ok && i <= 4; i++) { if (x[i] - y[i] > 2 || y[i] - x[i] > 2) ok = 0 } print ok ? "yes" : "no" }'
}
assign Virtual-0 "scrolling:"
sleep 2
open c1 400 300
xopen x1 400 300
sleep 0.5
[ "$(outof "$(geo x1)")" = Virtual-0 ] || sendto x1 Virtual-0 || stage_fail "x1 is not on Virtual-0 ($(geo x1))"
ids
for c in c1 x1; do
    rid="{11111111-2222-4333-8444-5555555f9891}"
    [ "$c" = x1 ] && rid="{11111111-2222-4333-8444-5555555f9892}"
    R='{"actions":[{"event":"window.movement.placeIn","durationMs":'"$PIN"',"type":"overrideAnimationTiming"},{"event":"window.movement.placeOut","durationMs":'"$POUT"',"type":"overrideAnimationTiming"}],"enabled":true,"id":"'"$rid"'","match":{"all":[{"field":"title","op":"equals","value":"'"$c"'"}]},"name":"probe F989 '"$c"'","priority":50}'
    [ "$(PZ org.plasmazones.Rules.addRule "$R")" = true ] || stage_fail "Rules.addRule refused the $c timing rule"
    on_exit "PZ org.plasmazones.Rules.removeRule '$rid' >/dev/null"
done
sleep 1.5
echo "Virtual-0 strip: c1 $(geo c1) | x1 $(geo x1)"

# press CAPTION: the maximize and the restore, with their legs checked
press() {
    local c="$1" u pre ar mx rest l from dur delta px
    u=$(uuid "$c")
    [ -n "$u" ] || stage_fail "$c never mapped"
    activate "$c"
    sleep 0.2
    pre=$(geo "$c")
    ar=$(area "$c")
    echo "  $c PRE $pre | AREA $ar"
    [ "$(maxof "$pre")" = 0 ] && [ "$(outof "$pre")" = Virtual-0 ] || stage_fail "$c is not an unmaximized tile on Virtual-0"
    emark
    setmax "$c" true
    sleep 1.2
    mx=$(geo "$c")
    sleep 1.5
    echo "  maximize: MAX $mx | legs: $(legs | head -4 | tr '\n' ';')"
    check "$c maximize: claiming lines" "$(elog "Maximize interception: claiming [^ ]*$u" | wc -l)" -eq 1
    delta=$(elog "view delta [-0-9]+ on strip" | head -1 | awk '{ print $3 }')
    read -r px _ <<< "$pre"
    l=$(legtoward "$(rect "$mx")")
    check "$c maximize: a leg toward MAX" "$(yesno "[ -n '$l' ]")" = yes
    if [ -n "$l" ]; then
        from=$(echo "$l" | cut -d'|' -f1)
        dur=$(echo "$l" | cut -d'|' -f3)
        check "$c maximize leg departs from PRE backed out by the view delta (${delta:-0})" \
            "$(near "$from" "$((${px%.*} - ${delta:-0})) $(rect "$pre" | cut -d' ' -f2-)")" = yes
        check "$c maximize leg departs from the maximize area" "$(near "$from" "$ar")" = no
        check "$c maximize leg duration (placeIn)" "$dur" -eq "$PIN"
    fi
    emark
    setmax "$c" false
    sleep 1.2
    rest=$(geo "$c")
    sleep 2
    echo "  restore: REST $rest | legs: $(legs | head -4 | tr '\n' ';')"
    check "$c restore: claiming lines" "$(elog "Maximize interception: claiming [^ ]*$u" | wc -l)" -eq 1
    l=$(legtoward "$(rect "$rest")")
    check "$c restore: a leg toward REST" "$(yesno "[ -n '$l' ]")" = yes
    if [ -n "$l" ]; then
        from=$(echo "$l" | cut -d'|' -f1)
        dur=$(echo "$l" | cut -d'|' -f3)
        check "$c restore leg departs from MAX" "$(near "$from" "$(rect "$mx")")" = yes
        check "$c restore leg duration (placeOut)" "$dur" -eq "$POUT"
    fi
}
echo "== c1 (Wayland)"
press c1
echo "== x1 (X11)"
press x1
finish
