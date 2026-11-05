#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: GPL-3.0-or-later
#
# f589-leg-ends-at-grab: a placement leg still running ends where it is drawn when a move grab, a
# resize grab (F589) or a maximize (F674) takes the window. Runner: 1 output, flags F S.
# Staging: snapping Virtual-0, Snap Assist off, p1 = plain_gtk.py e16. Two rules (title equals
#   e16) stretch its placeIn and placeOut legs; each part gates on the leg's logged duration being
#   at least 2000 ms. Part 1: p1 snapped into zone 1 and settled, then floated (Snap.setWindowFloat,
#   a placeOut leg); as the leg starts a Meta+left drag grabs it at the leg's target, moves 150 px
#   and holds past the leg's end. Part 2: p1 snapped into zone 1 again (a placeIn leg) and grabbed
#   the same way with Meta+right (modifier resize). Part 3: p1 floated again; a KWin script
#   maximizes it while the leg runs.
# Asserts: in every part the animator logs no completion at the leg's target, no retarget and no
#   replace after the gesture (removeAnimation is silent); part 3 ends with p1 maximized.
# A FAIL means: the leg ran on to its target while the window was under the pointer or maximized.
set -u
NEST="$1"
LOG="$2"
. "$(dirname "$0")/../lib.sh"
kwjs_ok
setting snapAssistEnabled false
open e16 400 300
ids
P1U=$(uuid e16)
[ -n "$P1U" ] || stage_fail "e16 never mapped"
P1="org.pz.plain|$P1U"
RIN="{11111111-2222-4333-8444-5555555e1601}"
ROUT="{11111111-2222-4333-8444-5555555e1602}"
for pair in "$RIN window.movement.placeIn" "$ROUT window.movement.placeOut"; do
    read -r rid ev <<< "$pair"
    R='{"actions":[{"event":"'"$ev"'","durationMs":2500,"type":"overrideAnimationTiming"}],"enabled":true,"id":"'"$rid"'","match":{"all":[{"field":"title","op":"equals","value":"e16"}]},"name":"probe F589 '"$ev"'","priority":50}'
    [ "$(PZ org.plasmazones.Rules.addRule "$R")" = true ] || stage_fail "Rules.addRule refused the $ev timing rule"
    on_exit "PZ org.plasmazones.Rules.removeRule '$rid' >/dev/null"
done
sleep 1.5
# ranon TO: "yes" when, since emark, a completion at TO, a retarget or a replace comes before any
# fresh "Started animation"; "no" otherwise
ranon() {
    local lines ran
    lines=$(elog "Window snap animation [a-z]*:|Started animation from ")
    [ -n "$lines" ] && echo "$lines" | head -8 | cut -c1-170 | sed 's/^/    /'
    ran=$(echo "$lines" | awk -v to="$1" '
        /^Started animation/ { print "no"; exit }
        /snap animation (retargeted|replaced):/ { print "yes"; exit }
        /snap animation complete:/ { if (index($0, "target: " to) > 0) { print "yes"; exit } }')
    echo "${ran:-no}"
}
# gate LEG LABEL: the leg started and runs the stretched duration
gate() {
    [ -n "$1" ] || stage_fail "no animated $2 leg (animations off, or the apply was not animated)"
    local dur=${1##*|}
    [[ "$dur" =~ ^[0-9]+$ ]] && [ "$dur" -ge 2000 ] || stage_fail "the $2 leg runs ${dur} ms, the timing rule did not take"
}

echo "== part 1: F589, a move grab during a placeOut leg"
PZ org.plasmazones.Control.snapWindowToZone "$P1" 1 Virtual-0 >/dev/null
sleep 3.5
Z=$(zoneof "$P1")
echo "p1 snapped: $(geo e16) zone=$Z"
case "$Z" in "{"*) ;; *) stage_fail "p1 did not snap ($Z)" ;; esac
emark
PZ org.plasmazones.Snap.setWindowFloat "$P1" true >/dev/null
LEG=$(firstleg)
echo "float leg: $LEG"
gate "$LEG" placeOut
TO=$(echo "$LEG" | cut -d'|' -f2)
read -r GX GY <<< "$(qrectf_centre "$TO")"
emark
# KEY_LEFTMETA = 125. Grab, move 150 px, hold until past the leg's end, release.
fake abs "$GX" "$GY" wait 60 keydown 125 wait 40 down left wait 80 \
    path "$GX" "$GY" $((GX + 150)) $((GY + 60)) 10 30 wait 2600 up left wait 50 keyup 125
echo "p1 after the drop: $(geo e16) floating=$(floating "$P1")"
check "part 1: the placeOut leg ran on after the move grab" "$(ranon "$TO" | tail -1)" = no
sleep 1

echo "== part 2: F589, a resize grab during a placeIn leg"
emark
PZ org.plasmazones.Control.snapWindowToZone "$P1" 1 Virtual-0 >/dev/null
LEG=$(firstleg)
echo "snap leg: $LEG"
gate "$LEG" placeIn
TO=$(echo "$LEG" | cut -d'|' -f2)
read -r GX GY <<< "$(qrectf_centre "$TO")"
emark
fake abs "$GX" "$GY" wait 60 keydown 125 wait 40 down right wait 80 \
    path "$GX" "$GY" $((GX + 120)) $((GY + 80)) 10 30 wait 2600 up right wait 50 keyup 125
echo "p1 after the release: $(geo e16) zone=$(zoneof "$P1")"
check "part 2: the placeIn leg ran on after the resize grab" "$(ranon "$TO" | tail -1)" = no
sleep 1

echo "== part 3: F674, a maximize during a placeOut leg"
PZ org.plasmazones.Control.snapWindowToZone "$P1" 1 Virtual-0 >/dev/null
sleep 3.5
Z=$(zoneof "$P1")
echo "p1 snapped: $(geo e16) zone=$Z"
case "$Z" in "{"*) ;; *) stage_fail "p1 did not snap again before part 3 ($Z)" ;; esac
emark
PZ org.plasmazones.Snap.setWindowFloat "$P1" true >/dev/null
LEG=$(firstleg)
echo "float leg: $LEG"
gate "$LEG" placeOut
TO=$(echo "$LEG" | cut -d'|' -f2)
emark
kwjs 'const w = workspace.windowList(); for (var i = 0; i < w.length; i++) { if (w[i].caption === "e16") { w[i].setMaximize(true, true); } }'
sleep 3
G=$(geo e16)
echo "p1 after the maximize: $G"
check "part 3: p1 maximized" "$(maxof "$G")" = 3
check "part 3: the placeOut leg ran on after the maximize" "$(ranon "$TO" | tail -1)" = no
finish
