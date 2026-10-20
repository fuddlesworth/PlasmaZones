#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: GPL-3.0-or-later
#
# f1123-bus-float-other-screen: the screen-form bus float, from a caller other than the compositor
# bridge, is refused for a screen the window is not on; naming its own screen still floats it, and
# the effect's own floats are not held. Runner: 2 outputs, no flags.
# Staging: Snap Assist off. Virtual-1 (A) COLS3, Virtual-0 (B) autotile:bsp with one tile t1. w
#   opens on A and is snapped into zone 1. From this shell (gdbus) WindowTracking.
#   setWindowFloatingForScreen(w, Virtual-0, true), then the same call naming Virtual-1, then t1 is
#   minimized (the effect floats it over the same verb).
# Asserts: the Virtual-0 call logs "<w> is on Virtual-1 not Virtual-0 - refused for" and no
#   floating= true send, B's tiles do not name w, w stays in zone 1 and not floating. The
#   Virtual-1 call floats w (one send, no refusal, w floating in no zone). The minimize of t1 logs
#   a send and no refusal.
# A FAIL means: B's autotile engine adopted w floating while it sat on A.
set -u
NEST="$1"
LOG="$2"
. "$(dirname "$0")/../lib.sh"
kwjs_ok
need_outputs 2
setting snapAssistEnabled false
assign Virtual-1 "$COLS3"
assign Virtual-0 "autotile:bsp"
sleep 2
open t1
sendto t1 Virtual-0 || stage_fail "t1 not on Virtual-0 ($(geo t1))"
open w
sendto w Virtual-1 || stage_fail "w not on Virtual-1 ($(geo w))"
sleep 1.5
ids
WU=$(uuid w)
T1U=$(uuid t1)
[ -n "$WU" ] && [ -n "$T1U" ] || stage_fail "w and t1 did not both map"
W="org.pz.plain|$WU"
PZ org.plasmazones.Control.snapWindowToZone "$W" 1 Virtual-1 >/dev/null
sleep 2
GW=$(geo w)
Z=$(zoneof "$W")
echo "staged: w (...${WU: -8}) $GW zone=$Z floating=$(floating "$W") | t1 $(geo t1) | B tiles $(tileorder Virtual-0)"
case "$Z" in "{"*) ;; *) stage_fail "w did not snap on Virtual-1 ($Z)" ;; esac
[ "$(outof "$GW")" = Virtual-1 ] || stage_fail "w is not on Virtual-1 ($GW)"
float_lines() {
    dlog "setWindowFloatingForScreen: [^ ]*$1.*|setWindowFloatingForScreen: windowId= [^ ]*$1.*" | sed 's/org.pz.plain|[^ ]*/<w>/; s/^/    /'
}
echo "== bus float of w naming Virtual-0 (B), from this shell"
dmark
echo "  reply: $(gdbus call --session --dest org.plasmazones --object-path /PlasmaZones --method org.plasmazones.WindowTracking.setWindowFloatingForScreen "$W" Virtual-0 true 2>&1 | tr -d '\n')"
sleep 2
float_lines "$WU"
check "refusal lines for w naming the screen it is on" "$(dlog "setWindowFloatingForScreen: [^ ]*$WU.* is on .*refused for" | wc -l)" -eq 1
check "floating= true sends for w" "$(floatsends "$WU")" -eq 0
check "B's tiles do not name w" "$(tileorder Virtual-0)" '!~' '(^| )w(\(float\))?( |$)'
check "w's zone after the refused call" "$(zoneof "$W")" = "$Z"
check "isWindowFloating w after the refused call" "$(floating "$W")" = false
echo "== control: bus float of w naming Virtual-1 (its own screen)"
dmark
gdbus call --session --dest org.plasmazones --object-path /PlasmaZones --method org.plasmazones.WindowTracking.setWindowFloatingForScreen "$W" Virtual-1 true >/dev/null 2>&1
sleep 2
float_lines "$WU"
check "floating= true sends for w naming its own screen" "$(floatsends "$WU")" -eq 1
check "refusal lines for w naming its own screen" "$(dlog "setWindowFloatingForScreen: [^ ]*$WU.*refused for" | wc -l)" -eq 0
check "isWindowFloating w after its own-screen call" "$(floating "$W")" = true
check "w's zone after its own-screen call" "$(zoneof "$W")" = ""
echo "== control: t1 minimized (the effect's own float)"
dmark
on t1 'w[i].minimized = true;'
sleep 2
check "floating= true sends for the minimized t1" "$(floatsends "$T1U")" -ge 1
check "refusal lines for t1" "$(dlog "setWindowFloatingForScreen: [^ ]*$T1U.*refused" | wc -l)" -eq 0
finish
