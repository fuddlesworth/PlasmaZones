#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: GPL-3.0-or-later
#
# f1147-arrival-minimize-float: an arrival re-seat gives back the evacuee's minimize-float (with
# F1148: the effect files the minimize-float for the output the window left). Runner: 2 outputs, no
# flags.
# Staging: Snap Assist off. Virtual-1 (A) autotile:bsp with w and x tiled; Virtual-0 (B) COLS3, so the
#   effect does not track w on B. w is minimized (a minimize-float), then A is disabled: both land on
#   B. w is unminimized on B. A is enabled; once the daemon has made A's sensor surface it is frozen
#   (dstop) until the effect's reportOutputSettle call times out (Qt's 25 s default), so w is
#   announced on A with no verdict. Let run, the daemon answers the late report and its
#   readoptEvacuee seats w (parkReseated, then parkDropped) while the effect's announce of w on A is
#   in flight: the arrival re-seat arm.
# Asserts: at the removal the effect files "Evacuee <w> parked for Virtual-1 ... minimize-float:
#   true"; after the return it logs "Park re-seat for <w> on Virtual-1 inside its announce there: the
#   arrival re-seat" and "Evacuee minimize-float of <w> on Virtual-1 taken for its return, given back:
#   true", the daemon sends setWindowFloatingForScreen(w, floating= false), w reads not floating and
#   A's tiles name w beside x.
# A FAIL means: the effect kept the connected output's entries for a Readopt verdict that never came,
#   and w stayed floating over its slot.
set -u
NEST="$1"
LOG="$2"
. "$(dirname "$0")/../lib.sh"
kwjs_ok
need_outputs 2
dpid >/dev/null || stage_fail "no gated nested daemon pid (the frozen settle needs it)"
on_exit 'ksd output.Virtual-1.enable >/dev/null'
setting snapAssistEnabled false
assign Virtual-1 "autotile:bsp"
assign Virtual-0 "$COLS3"
sleep 2
for c in w x; do
    open "$c"
    sendto "$c" Virtual-1 || stage_fail "$c not on Virtual-1 ($(geo "$c"))"
done
sleep 2
ids
WU=$(uuid w)
XU=$(uuid x)
[ -n "$WU" ] && [ -n "$XU" ] || stage_fail "w or x never mapped"
W="org.pz.plain|$WU"
echo "staged: w (...${WU: -8}) $(geo w) | x $(geo x) | A tiles $(tileorder Virtual-1)"
case "$(tiles Virtual-1)" in 2*) ;; *) stage_fail "Virtual-1 does not hold two tiles" ;; esac
on w 'w[i].minimized = true;'
sleep 2
echo "w minimized: $(geo w) floating=$(floating "$W")"
[ "$(floating "$W")" = true ] || stage_fail "w is not minimize-floated"
MON="$NEST/f1147-monitor.log"
mon_start "$MON" "type='signal',interface='org.plasmazones.WindowTracking',member='parkDropped'" \
    "type='signal',interface='org.plasmazones.WindowTracking',member='parkReseated'"
echo "== Virtual-1 disabled"
emark
dmark
ksd output.Virtual-1.disable
sleep 4
echo "  w $(geo w) | x $(geo x)"
settlerows "$WU"
settlerows "$XU"
[ "$(outof "$(geo w)")" = Virtual-0 ] || stage_fail "w was not evacuated to Virtual-0"
elog "Evacuee [^ ]*$WU.*" | sed 's/org.pz.plain|[^ "]*/<w>/g; s/^/    /'
check "effect filed w's minimize-float for Virtual-1" \
    "$(elog "Evacuee [^ ]*$WU[^ ]* parked for [^ ]*Virtual-1[^ ]* monocle claim: [a-z]+ minimize-float: true" | wc -l)" -ge 1
echo "== w unminimized on B"
on w 'w[i].minimized = false;'
sleep 2.5
echo "  w $(geo w) floating=$(floating "$W")"
echo "== Virtual-1 enabled; the daemon frozen once it sees A"
emark
dmark
mmark "$MON"
ksd output.Virtual-1.enable
# At most 0.25 s: the effect's settle debounce is 500 ms from the output change, and its report must
# go out after the freeze, or the daemon answers it in time and the Readopt verdict path runs instead.
for _ in 1 2 3 4 5; do
    dlog "Created layer surface: \"phosphor-screens-sensor-Virtual-1\"" | grep -q . && break
    sleep 0.05
done
SAW=$(dlog "Created layer surface: \"phosphor-screens-sensor-Virtual-1\"" | wc -l)
dstop || stage_fail "the daemon could not be frozen"
FREEZE=$(($(wc -l < "$LOG") + 1))
sleep 27
REPORTED=$(tail -n +"$FREEZE" "$LOG" | grep -c "Settle: reporting")
FAILED=$(tail -n +"$EMARK" "$LOG" | grep -c "reportOutputSettle failed")
echo "  sensor surface before the freeze: $SAW | settle reports while frozen: $REPORTED | 'reportOutputSettle failed' warnings: $FAILED"
dcont
sleep 5
AW=$(geo w)
echo "  w $AW floating=$(floating "$W") | x $(geo x) | A tiles $(tileorder Virtual-1)"
settlerows "$WU"
msgs "$MON" | grep "$WU" | sed 's/org.pz.plain|[^ ]*/<w>/; s/^/    /'
elog "(Park re-seat for|Park drop for|Evacuee) .*$WU.*" | sed 's/org.pz.plain|[^ "]*/<w>/g; s/^/    /'
[ "$SAW" -ge 1 ] || stage_fail "the daemon was frozen before it saw A; its late settle may not seat w"
[ "$FAILED" -ge 1 ] || stage_fail "the settle reply was not lost; the Readopt verdict path ran instead of the arrival re-seat"
[ "$(outof "$AW")" = Virtual-1 ] || stage_fail "KWin did not return w to A ($AW)"
check "arrival re-seat lines for w on Virtual-1" \
    "$(elog "Park re-seat for [^ ]*$WU[^ ]* on [^ ]*Virtual-1[^ ]* inside its announce there: the arrival re-seat" | wc -l)" -ge 1
check "give-back lines for w's minimize-float on Virtual-1" \
    "$(elog "Evacuee minimize-float of [^ ]*$WU[^ ]* on [^ ]*Virtual-1[^ ]* taken for its return, given back: true" | wc -l)" -eq 1
check "floating= false sends for w since the return" "$(floatsends "$WU" false)" -ge 1
check "isWindowFloating(w) after the return" "$(floating "$W")" = false
check "A's tiles name w as a tile" "$(yesno 'tileorder Virtual-1 | grep -Eq "(^| )w( |\$)"')" = yes
finish
