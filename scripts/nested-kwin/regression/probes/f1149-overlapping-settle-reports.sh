#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: GPL-3.0-or-later
#
# f1149-overlapping-settle-reports: a second settle report still out when the first is answered holds
# its Readopt's park drop for its own verdict, so a minimize-floated tile gets its float back.
# Runner: 4 outputs, no flags.
# Staging: Snap Assist off. Outputs left to right: Virtual-0 (B) autotile:bsp, Virtual-1 (A)
#   autotile:bsp, Virtual-2 (C) and Virtual-3 (D) COLS3 with z on D. w and x tile on A; w is minimized
#   (a minimize-float). A and C are disabled in one change (layout {B,D}): w and x land on B. A alone
#   is enabled (layout {A,B,D}, which KWin never saved): KWin restores nothing and the running daemon
#   learns A again. The daemon is frozen (dstop); D is disabled (report 1 goes out and waits), then C
#   and D are enabled in one change (layout {A,B,C,D}, the one w was saved under): KWin returns w and
#   x to A and report 2 goes out. The daemon is let run: it answers report 1, then report 2 (w's
#   Readopt: parkDropped(w, A), then the reply). w is unminimized on A afterwards.
# Asserts: the effect logs "Park drop for <w> on Virtual-1 inside the settle: its verdict decides" and
#   "Evacuee minimize-float of <w> on Virtual-1 taken for its return, given back: true", and no touch
#   forget of w's minimize-float; after the unminimize the daemon sends
#   setWindowFloatingForScreen(w, floating= false), w reads not floating and A's tiles name w.
# A FAIL means: report 1's answer cleared the single in-flight flag, report 2's drop was read as a touch
#   and forgotten, and w stayed floating over its slot.
set -u
NEST="$1"
LOG="$2"
. "$(dirname "$0")/../lib.sh"
kwjs_ok
need_outputs 4
dpid >/dev/null || stage_fail "no gated nested daemon pid (the overlapping reports need the freeze)"
on_exit 'ksd output.Virtual-1.enable output.Virtual-2.enable output.Virtual-3.enable >/dev/null'
setting snapAssistEnabled false
assign Virtual-0 "autotile:bsp"
assign Virtual-1 "autotile:bsp"
assign Virtual-2 "$COLS3"
assign Virtual-3 "$COLS3"
sleep 2
for c in w x; do
    open "$c"
    sendto "$c" Virtual-1 || stage_fail "$c not on Virtual-1 ($(geo "$c"))"
done
open z
sendto z Virtual-3 || stage_fail "z not on Virtual-3 ($(geo z))"
sleep 2
ids
WU=$(uuid w)
XU=$(uuid x)
[ -n "$WU" ] && [ -n "$XU" ] || stage_fail "w or x never mapped"
W="org.pz.plain|$WU"
echo "staged: w (...${WU: -8}) $(geo w) | x $(geo x) | z $(geo z) | A tiles $(tileorder Virtual-1)"
case "$(tiles Virtual-1)" in 2*) ;; *) stage_fail "Virtual-1 does not hold two tiles" ;; esac
on w 'w[i].minimized = true;'
sleep 2
echo "w minimized: $(geo w) floating=$(floating "$W")"
[ "$(floating "$W")" = true ] || stage_fail "w is not minimize-floated"
MON="$NEST/f1149-monitor.log"
mon_start "$MON" "type='signal',interface='org.plasmazones.WindowTracking',member='parkDropped'" \
    "type='signal',interface='org.plasmazones.WindowTracking',member='parkReseated'"
echo "== A (Virtual-1) and C (Virtual-2) disabled in one change"
emark
dmark
ksd output.Virtual-1.disable output.Virtual-2.disable
sleep 4
echo "  outputs: $(screens)"
echo "  w $(geo w) | x $(geo x)"
settlerows "$WU"
settlerows "$XU"
[ "$(outof "$(geo w)")" = Virtual-0 ] || stage_fail "w was not re-homed onto Virtual-0 ($(geo w))"
elog "Evacuee [^ ]*$WU.*" | sed 's/org.pz.plain|[^ "]*/<w>/g; s/^/    /'
echo "== A enabled alone (a layout KWin never saved)"
dmark
ksd output.Virtual-1.enable
sleep 4
echo "  outputs: $(screens)"
echo "  w $(geo w) | x $(geo x)"
settlerows "$WU"
[ "$(outof "$(geo w)")" != Virtual-1 ] || stage_fail "KWin returned w at the {A,B,D} layout; the return cannot be held for a second report"
echo "== daemon frozen; D disabled (report 1), then C and D enabled (report 2, w's return)"
dstop || stage_fail "the daemon could not be frozen"
FREEZE=$(($(wc -l < "$LOG") + 1))
emark
dmark
mmark "$MON"
ksd output.Virtual-3.disable
sleep 1.2
ksd output.Virtual-2.enable output.Virtual-3.enable
sleep 1.2
REPORTS=$(tail -n +"$FREEZE" "$LOG" | grep -c "Settle: reporting")
tail -n +"$FREEZE" "$LOG" | grep -o "Settle: reporting.*" | sed 's/^/    /'
echo "  w while frozen: $(geo w)"
dcont
sleep 5
AW=$(geo w)
echo "  outputs: $(screens)"
echo "  w $AW floating=$(floating "$W") | x $(geo x) | A tiles $(tileorder Virtual-1)"
settlerows "$WU"
V=$(verdictof "$WU")
msgs "$MON" | grep "$WU" | sed 's/org.pz.plain|[^ ]*/<w>/; s/^/    /'
elog "(Park drop for|Park re-seat for|Evacuee) .*$WU.*|reportOutputSettle failed.*" | sed 's/org.pz.plain|[^ "]*/<w>/g; s/^/    /'
[ "$REPORTS" -ge 2 ] || stage_fail "fewer than two settle reports went out while the daemon was frozen ($REPORTS); the reports did not overlap"
[ "$V" = 1 ] || stage_fail "w's return row is not verdict 1 (${V:-no row}); report 2 carried no Readopt"
[ "$(outof "$AW")" = Virtual-1 ] || stage_fail "KWin did not return w to A ($AW)"
check "in-settle park drops for w on Virtual-1" \
    "$(elog "Park drop for [^ ]*$WU[^ ]* on [^ ]*Virtual-1[^ ]* inside the settle" | wc -l)" -ge 1
check "give-back lines for w's minimize-float on Virtual-1" \
    "$(elog "Evacuee minimize-float of [^ ]*$WU[^ ]* on [^ ]*Virtual-1[^ ]* taken for its return, given back: true" | wc -l)" -eq 1
check "touch forgets of w's minimize-float on Virtual-1" \
    "$(elog "Evacuee park on [^ ]*Virtual-1[^ ]* forgotten for [^ ]*$WU[^ ]* .*minimize-float: true" | wc -l)" -eq 0
echo "== w unminimized on A"
dmark
on w 'w[i].minimized = false;'
sleep 3
echo "  w $(geo w) | A tiles $(tileorder Virtual-1)"
check "floating= false sends for w since the unminimize" "$(floatsends "$WU" false)" -ge 1
check "isWindowFloating(w) after the unminimize" "$(floating "$W")" = false
check "A's tiles name w as a tile" "$(yesno 'tileorder Virtual-1 | grep -Eq "(^| )w( |\$)"')" = yes
finish
