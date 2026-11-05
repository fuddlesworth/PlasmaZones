#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: GPL-3.0-or-later
#
# f1138-float-toggle-screen-hint: Snap.toggleFloatForWindow with a screen hint the window is not on is
# refused, for a float and for an unfloat. Runner: 2 outputs, no flags.
# Staging: Snap Assist off. Virtual-1 (A) and Virtual-0 (B) both COLS3, so the "snapping does not run"
#   refusal cannot stand in for this one. w opens on A and is snapped into zone 1; u opens on A, is
#   snapped into zone 2 and floated with the toggle naming A, its own screen.
# Asserts: toggleFloatForWindow(w, Virtual-0) logs "Bus float of <w> refused: the window is not on" and
#   w stays in zone 1, not floating; toggleFloatForWindow(u, Virtual-0) is refused the same way and u
#   stays floating on A in no zone. Control: toggleFloatForWindow(w, Virtual-1) is not refused and
#   floats w.
# A FAIL means: the toggle ran against B's context, floating w or unfloating u into a zone on B.
set -u
NEST="$1"
LOG="$2"
. "$(dirname "$0")/../lib.sh"
kwjs_ok
need_outputs 2
setting snapAssistEnabled false
assign Virtual-1 "$COLS3"
assign Virtual-0 "$COLS3"
sleep 2
for c in w u; do
    open "$c"
    sendto "$c" Virtual-1 || stage_fail "$c not on Virtual-1 ($(geo "$c"))"
done
sleep 1.5
ids
WU=$(uuid w)
UU=$(uuid u)
[ -n "$WU" ] && [ -n "$UU" ] || stage_fail "w or u never mapped"
W="org.pz.plain|$WU"
U="org.pz.plain|$UU"
toggle() {
    gdbus call --session --dest org.plasmazones --object-path /PlasmaZones \
        --method org.plasmazones.Snap.toggleFloatForWindow "$1" "$2" 2>&1 | tr -d '\n'
}
refusals() { dlog "Bus float of [^ ]*$1[^ ]* refused: the window is not on" | wc -l; }
PZ org.plasmazones.Control.snapWindowToZone "$W" 1 Virtual-1 >/dev/null
PZ org.plasmazones.Control.snapWindowToZone "$U" 2 Virtual-1 >/dev/null
sleep 2
echo "u floated with the toggle naming Virtual-1: $(toggle "$U" Virtual-1)"
sleep 2
ZW=$(zoneof "$W")
GU=$(geo u)
echo "staged: w $(geo w) zone=$ZW floating=$(floating "$W") | u $GU zone='$(zoneof "$U")' floating=$(floating "$U")"
case "$ZW" in "{"*) ;; *) stage_fail "w did not snap on Virtual-1 ($ZW)" ;; esac
[ "$(floating "$U")" = true ] || stage_fail "u is not floating on Virtual-1 (the toggle naming its own screen did not float it)"
echo "== toggle of w (snapped on A) naming Virtual-0"
dmark
echo "  reply: $(toggle "$W" Virtual-0)"
sleep 2
dlog "toggleFloatForWindow: windowId= [^ ]*$WU.*|Bus float of [^ ]*$WU.*" | sed 's/org.pz.plain|[^ ]*/<w>/; s/^/    /'
check "refusal lines for w" "$(refusals "$WU")" -eq 1
check "w floating after the refused toggle" "$(floating "$W")" = false
check "w's zone after the refused toggle" "$(zoneof "$W")" = "$ZW"
echo "== toggle of u (floating on A) naming Virtual-0"
dmark
echo "  reply: $(toggle "$U" Virtual-0)"
sleep 2
AU=$(geo u)
dlog "toggleFloatForWindow: windowId= [^ ]*$UU.*|Bus float of [^ ]*$UU.*" | sed 's/org.pz.plain|[^ ]*/<u>/; s/^/    /'
check "refusal lines for u" "$(refusals "$UU")" -eq 1
check "u floating after the refused toggle" "$(floating "$U")" = true
check "u's zone after the refused toggle" "$(zoneof "$U")" = ""
check "u's output after the refused toggle" "$(outof "$AU")" = Virtual-1
check "u's frame after the refused toggle" "$(rect "$AU")" = "$(rect "$GU")"
echo "== control: toggle of w naming Virtual-1 (its own screen)"
dmark
toggle "$W" Virtual-1 >/dev/null
sleep 2
echo "  w $(geo w)"
check "refusal lines for w naming its own screen" "$(dlog "Bus float of [^ ]*$WU[^ ]* refused" | wc -l)" -eq 0
check "w floating after the toggle naming its own screen" "$(floating "$W")" = true
finish
