#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: GPL-3.0-or-later
#
# f670-snap-float-evacuee: a snap float on a monitor that goes away stays a snap float on the
# monitor KWin moves it to. Runner: 2 outputs, no flags.
# Staging: Virtual-0 and Virtual-1 both COLS3, Snap Assist off. p1 is sent to Virtual-1, snapped
#   into its zone 1 and floated with the float shortcut. Virtual-1 is disabled with kscreen-doctor,
#   so KWin moves p1 onto Virtual-0 and the effect reports the settle. Virtual-1 then comes back
#   with p1 untouched (KWin returns it), goes away again, and the float shortcut is pressed on p1
#   on Virtual-0.
# Asserts: after each settle on Virtual-0 isWindowFloating p1 is true with no zone; after the
#   return p1 is back on Virtual-1, floating; the float shortcut on the evacuee is answered by
#   snapping (its navigationFeedback is not "not_managed"), and p1 ends floating or in a zone.
# A FAIL means: the park prune took the float bit, so p1 read not floating, snapping did not track
#   it and the float shortcut answered "not_managed".
set -u
NEST="$1"
LOG="$2"
. "$(dirname "$0")/../lib.sh"
kwjs_ok
need_outputs 2
on_exit 'ksd output.Virtual-1.enable >/dev/null'
setting snapAssistEnabled false
assign Virtual-0 "$COLS3"
assign Virtual-1 "$COLS3"
sleep 2
open p1 300 300
sendto p1 Virtual-1 || stage_fail "p1 did not reach Virtual-1 ($(geo p1))"
ids
P1U=$(uuid p1)
[ -n "$P1U" ] || stage_fail "p1 never mapped"
P1="org.pz.plain|$P1U"
PZ org.plasmazones.Control.snapWindowToZone "$P1" 1 Virtual-1 >/dev/null
sleep 1.5
echo "p1 snapped in Virtual-1 zone 1: $(wstat p1 "$P1")"
case "$(zoneof "$P1")" in "{"*) ;; *) stage_fail "p1 did not snap on Virtual-1" ;; esac
activate p1
shortcut toggle_window_float
sleep 2
echo "p1 floated on Virtual-1: $(wstat p1 "$P1")"
[ "$(floating "$P1")" = true ] || stage_fail "the float shortcut did not float p1 on Virtual-1"

echo "== Virtual-1 disabled"
dmark
ksd output.Virtual-1.disable
sleep 4
settlerows "$P1U"
[ "$(outof "$(geo p1)")" = Virtual-0 ] || stage_fail "KWin did not move p1 onto Virtual-0 ($(geo p1))"
echo "p1 after the settle: $(wstat p1 "$P1")"
check "p1 floating after the settle on Virtual-0" "$(floating "$P1")" = true
check "p1's zone after the settle on Virtual-0" "$(zoneof "$P1")" = ""

echo "== Virtual-1 back, p1 untouched"
ksd output.Virtual-1.enable
sleep 4
echo "p1 after the return: $(wstat p1 "$P1")"
[ "$(outof "$(geo p1)")" = Virtual-1 ] || stage_fail "KWin did not return p1 to Virtual-1 ($(geo p1))"
check "p1 floating after KWin returns it to Virtual-1" "$(floating "$P1")" = true

echo "== Virtual-1 away again, then the float shortcut on p1"
ksd output.Virtual-1.disable
sleep 4
[ "$(outof "$(geo p1)")" = Virtual-0 ] || stage_fail "KWin did not move p1 onto Virtual-0 the second time ($(geo p1))"
echo "p1 after the second settle: $(wstat p1 "$P1")"
check "p1 floating after the second settle" "$(floating "$P1")" = true
MON="$NEST/f670-monitor.log"
mon_start "$MON" "type='signal',interface='org.plasmazones.WindowTracking',member='navigationFeedback'"
mmark "$MON"
activate p1
shortcut toggle_window_float
sleep 2
FB=$(msgs "$MON" | grep "^navigationFeedback " | head -1)
echo "float feedback: ${FB:-none}"
[ -n "$FB" ] || note "the float shortcut sent no navigationFeedback"
check "the float shortcut's feedback on the evacuee" "$FB" '!~' "not_managed"
echo "p1 after the float shortcut: $(wstat p1 "$P1")"
check "p1 floating or in a zone after the float shortcut" \
    "$(yesno '[ "$(floating "$P1")" = true ] || case "$(zoneof "$P1")" in "{"*) true ;; *) false ;; esac')" = yes
finish
