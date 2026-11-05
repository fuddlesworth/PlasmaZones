#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: GPL-3.0-or-later
#
# f678-bus-float-engine: WindowTracking.setWindowFloating floats and unfloats through the engine
# that runs the window's screen. Runner: 2 outputs, no flags.
# Staging: Snap Assist off, Virtual-0 COLS3 (snapping), Virtual-1 autotile:bsp. t1 and t2 are sent
#   to Virtual-1 and must both be in its tiles; s1 is sent to Virtual-0 and snapped into zone 1. A
#   dbus-monitor records windowFloatingChanged. Then setWindowFloating(t1, true), (t1, false),
#   (s1, true) followed by the float shortcut on s1, and (org.pz.plain|nonexistent, true).
# Asserts: t1 floated leaves the bsp layout (t2 takes the screen, t1 out of the tiles, reads
#   floating) with a windowFloatingChanged true; unfloated it tiles again beside t2; s1 floated
#   leaves zone 1 with zone 1 kept as its pre-float zone, and the float shortcut snaps it back; the
#   phantom id is refused with "setWindowFloating: no live window or screen" and no signal names it.
# A FAIL means: the verb only recorded the float, so the tile stayed in the layout and the snapped
#   window kept its zone while subscribers heard it floated.
set -u
NEST="$1"
LOG="$2"
. "$(dirname "$0")/../lib.sh"
ZONE1="{2bb43b1d-3506-40b7-8f2b-e0fe59c0e90e}"
kwjs_ok
need_outputs 2
setting snapAssistEnabled false
assign Virtual-0 "$COLS3"
assign Virtual-1 "autotile:bsp"
sleep 2
for c in t1 t2 s1; do
    open "$c" 300 300
    case "$c" in t*) out=Virtual-1 ;; *) out=Virtual-0 ;; esac
    sendto "$c" "$out" || stage_fail "$c not on $out ($(geo "$c"))"
done
ids
T1=$(uuid t1)
T2=$(uuid t2)
S1=$(uuid s1)
[ -n "$T1" ] && [ -n "$T2" ] && [ -n "$S1" ] || stage_fail "a window never mapped"
PZ org.plasmazones.Snap.moveWindowToZone "org.pz.plain|$S1" "$ZONE1" >/dev/null
sleep 2
PAIR0=$(frames t1 t2)
TL0=$(tiles Virtual-1)
echo "t1/t2 on Virtual-1: $PAIR0 | tiles $TL0 | s1 $(geo s1)"
case "$TL0" in "2 "*"'${T1: -8}'"* | "2 "*"'${T2: -8}'"*) ;; *) stage_fail "t1/t2 not both in Virtual-1's tiles ($TL0)" ;; esac
case "$TL0" in *"'${T1: -8}'"*) ;; *) stage_fail "t1 not in Virtual-1's tiles ($TL0)" ;; esac
case "$TL0" in *"'${T2: -8}'"*) ;; *) stage_fail "t2 not in Virtual-1's tiles ($TL0)" ;; esac
[ "$(zoneof "org.pz.plain|$S1")" = "$ZONE1" ] || stage_fail "s1 not in zone 1"
MON="$NEST/a7-monitor.log"
mon_start "$MON" "type='signal',interface='org.plasmazones.Tiling',member='windowFloatingChanged'" \
    "type='signal',interface='org.plasmazones.WindowTracking',member='windowFloatingChanged'" \
    "type='signal',interface='org.plasmazones.WindowTracking',member='windowStateChanged'"
busfloat() {
    PZ org.plasmazones.WindowTracking.setWindowFloating "$1" "$2" >/dev/null
    sleep 2.5
}
echo "== setWindowFloating(t1, true)"
mmark "$MON"
busfloat "org.pz.plain|$T1" true
P1=$(frames t1 t2)
TL=$(tiles Virtual-1)
echo "  t1/t2: $P1 | tiles $TL"
check "t1 floated: the bsp layout changed (t2 takes the screen)" "$P1" != "$PAIR0"
check "t1 floated: t1 is out of Virtual-1's tiles" "$TL" '!~' "'${T1: -8}'"
check "t1 floated: t1 reads floating" "$(floating "org.pz.plain|$T1")" = true
check "t1 floated: windowFloatingChanged true for t1" "$(wfct "$MON" "$T1")" -ge 1
echo "== setWindowFloating(t1, false)"
busfloat "org.pz.plain|$T1" false
P2=$(frames t1 t2)
TL=$(tiles Virtual-1)
echo "  t1/t2: $P2 | tiles $TL"
check "t1 unfloated: both t1 and t2 are tiled on Virtual-1" "$TL" '~' "^2 "
check "t1 unfloated: t1 is in Virtual-1's tiles" "$TL" '~' "'${T1: -8}'"
check "t1 unfloated: t1 reads not floating" "$(floating "org.pz.plain|$T1")" = false
echo "== setWindowFloating(s1, true)"
busfloat "org.pz.plain|$S1" true
echo "  s1: $(geo s1)"
check "s1 floated: s1 reads floating" "$(floating "org.pz.plain|$S1")" = true
check "s1 floated: s1 left zone 1" "$(zoneof "org.pz.plain|$S1")" = ""
check "s1 floated: zone 1 is kept as its pre-float zone" \
    "$(PZ org.plasmazones.WindowTracking.getPreFloatZone "org.pz.plain|$S1" | paste -sd' ')" = "true $ZONE1"
activate s1
shortcut toggle_window_float
sleep 2.5
echo "  s1 after the float shortcut: $(geo s1)"
check "the float shortcut snaps s1 back into zone 1" "$(zoneof "org.pz.plain|$S1")" = "$ZONE1"
check "s1 reads not floating after the shortcut" "$(floating "org.pz.plain|$S1")" = false
echo "== setWindowFloating(org.pz.plain|nonexistent, true)"
dmark
mmark "$MON"
busfloat "org.pz.plain|nonexistent" true
check "the phantom id is refused" \
    "$(dlog "setWindowFloating: no live window or screen for \"org.pz.plain.nonexistent\" - refused" | wc -l)" -ge 1
check "no signal names the phantom id" "$(msgs "$MON" | grep -c "nonexistent")" = 0
finish
