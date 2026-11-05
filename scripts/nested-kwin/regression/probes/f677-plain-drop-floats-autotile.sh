#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: GPL-3.0-or-later
#
# f677-plain-drop-floats-autotile: a snapped window dragged with no trigger onto an autotile
# monitor floats there, at its pre-snap size, and leaves the tiles alone. Runner: 2 outputs, flag F.
# Staging: Snap Assist off, default triggers. Virtual-0 COLS3, Virtual-1 autotile:bsp with t1 and t2
#   (both in its tiles). p1 (its free size recorded) is snapped into zone 1 on Virtual-0. A
#   dbus-monitor records windowFloatingChanged. p1 is Meta+left dragged (no trigger) to Virtual-1's
#   middle, held 2.5 s and dropped; then the float shortcut on p1.
# Asserts: t1/t2 frames unchanged mid-drag and after the drop; the daemon logs "dragEndedOnScreen:
#   <p1> dropped on Virtual-1 floats in autotile" and "AutotileEngine::handoffReceive: <p1> to
#   Virtual-1 ... wasFloating= true"; p1 ends on Virtual-1 floating, with no zone, at its pre-snap
#   size; the float shortcut makes it a third tile on Virtual-1.
# A FAIL means: the drop was tiled by the crossing (the tiles reflowed), or p1 ended unmanaged with
#   snap's zone still recorded on Virtual-0.
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
for c in t1 t2 p1; do
    open "$c" 300 300
    case "$c" in t*) out=Virtual-1 ;; *) out=Virtual-0 ;; esac
    sendto "$c" "$out" || stage_fail "$c not on $out ($(geo "$c"))"
done
read -r V1X V1Y V1W V1H <<< "$(scr Virtual-1)"
ids
T1=$(uuid t1)
T2=$(uuid t2)
P1=$(uuid p1)
[ -n "$T1" ] && [ -n "$T2" ] && [ -n "$P1" ] || stage_fail "a window never mapped"
PRESNAP=$(size "$(geo p1)")
echo "p1 pre-snap size: $PRESNAP"
PZ org.plasmazones.Snap.moveWindowToZone "org.pz.plain|$P1" "$ZONE1" >/dev/null
sleep 2
[ "$(zoneof "org.pz.plain|$P1")" = "$ZONE1" ] || stage_fail "p1 not in zone 1"
PAIR0=$(frames t1 t2)
TL0=$(tiles Virtual-1)
echo "p1 snapped: $(geo p1) | t1/t2: $PAIR0 | tiles $TL0"
case "$TL0" in "2 "*"'${T1: -8}'"*) ;; *) stage_fail "t1/t2 not both in Virtual-1's tiles ($TL0)" ;; esac
case "$TL0" in *"'${T2: -8}'"*) ;; *) stage_fail "t2 not in Virtual-1's tiles ($TL0)" ;; esac
MON="$NEST/f677-monitor.log"
mon_start "$MON" "type='signal',interface='org.plasmazones.Tiling',member='windowFloatingChanged'" \
    "type='signal',interface='org.plasmazones.WindowTracking',member='windowFloatingChanged'"
read -r GX GY GW GH _ <<< "$(geo p1)"
GX=$((${GX%.*} + ${GW%.*} / 2))
GY=$((${GY%.*} + ${GH%.*} / 2))
DX=$((V1X + V1W / 2))
DY=$((V1Y + V1H / 2))
echo "== p1: Meta+left drag, no trigger, from $GX,$GY to $DX,$DY, held 2.5 s"
mmark "$MON"
dmark
# fake runs in the background here, so check its binary in this shell first.
[ -x "$PZR_FAKEINPUT" ] || stage_fail "no fakeinput binary (the probe needs flag F)"
fake abs "$GX" "$GY" wait 100 keydown 125 wait 50 down left wait 150 \
    path "$GX" "$GY" "$DX" "$DY" 20 30 wait 2500 up left wait 80 keyup 125 &
FIPID=$!
sleep 2.3
MID=$(frames t1 t2)
wait "$FIPID"
sleep 2.5
AFTER=$(frames t1 t2)
G=$(geo p1)
echo "  mid-drag: $MID | after the drop: p1 $G | $AFTER"
[ "$(outof "$G")" = Virtual-1 ] || stage_fail "the drag did not carry p1 onto Virtual-1 ($G)"
check "t1/t2 frames unchanged mid-drag" "$MID" = "$PAIR0"
check "t1/t2 frames unchanged after the drop" "$AFTER" = "$PAIR0"
check "the drop floats in autotile" \
    "$(dlog "dragEndedOnScreen: [^ ]*$P1[^ ]* dropped on \"Virtual-1\" floats in \"autotile\"" | wc -l)" -ge 1
check "autotile receives p1 as a float" \
    "$(dlog "AutotileEngine::handoffReceive: [^ ]*$P1[^ ]* to \"Virtual-1\" .*wasFloating= true" | wc -l)" -ge 1
check "p1 reads floating" "$(floating "org.pz.plain|$P1")" = true
check "p1 has no zone" "$(zoneof "org.pz.plain|$P1")" = ""
check "p1 is out of Virtual-1's tiles" "$(tiles Virtual-1)" '!~' "'${P1: -8}'"
check "p1 is back at its pre-snap size" "$(size "$G")" = "$PRESNAP"
check "windowFloatingChanged true for p1" "$(wfct "$MON" "$P1")" -ge 1
activate p1
shortcut toggle_window_float
sleep 2.5
TL=$(tiles Virtual-1)
echo "  after the float shortcut: p1 $(geo p1) | $(frames t1 t2) | tiles $TL"
check "the float shortcut tiles p1 as a third tile" "$TL" '~' "^3 "
check "the float shortcut lists p1 in Virtual-1's tiles" "$TL" '~' "'${P1: -8}'"
check "p1 reads not floating after the shortcut" "$(floating "org.pz.plain|$P1")" = false
finish
