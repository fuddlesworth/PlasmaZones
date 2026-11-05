#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: GPL-3.0-or-later
#
# f739-restart-keeps-records: a daemon restart keeps every live window's record, a kept-above one
# included (F739), and the bring-up reports each window's sticky state just ahead of its metadata
# (F610). Runner: 1 output, no flags.
# Staging: two desktops; Virtual-0 autotile:bsp with autotileStickyWindowHandling 2 (IgnoreAll).
#   Four windows: n1 (plain, tiled), k1 (keepAbove), f1 (fullScreen), s1 (onAllDesktops). A
#   dbus-monitor records setWindowSticky and setWindowMetadata across a daemon restart (dkill, then
#   drestart).
# Asserts: after the restart getWindowMetadata still answers org.pz.plain and its caption for all
#   four; the bring-up sends a setWindowSticky naming each window immediately before that window's
#   setWindowMetadata, true for s1 and false for the other three; s1 keeps its frame (not tiled).
# A FAIL means: the keep-above window was left off the alive list and pruned, or the bring-up sent
#   metadata with no sticky state ahead of it.
set -u
NEST="$1"
LOG="$2"
. "$(dirname "$0")/../lib.sh"
kwjs_ok
md() { qdbus6 org.plasmazones /PlasmaZones org.plasmazones.WindowTracking.getWindowMetadata "$1" 2>&1 | paste -sd'|'; }
newdesk 1 D2
assign Virtual-0 "autotile:bsp"
[ "$(PZ org.plasmazones.Settings.setSetting autotileStickyWindowHandling 2)" = true ] \
    || stage_fail "setSetting autotileStickyWindowHandling 2 was refused"
sleep 2
for c in n1 k1 f1 s1; do
    open "$c" 400 300
done
on k1 "w[i].keepAbove = true;"
on f1 "w[i].fullScreen = true;"
on s1 "w[i].onAllDesktops = true;"
sleep 2
ids
declare -A ID
for c in n1 k1 f1 s1; do
    u=$(uuid "$c")
    [ -n "$u" ] || stage_fail "$c never mapped"
    ID[$c]="org.pz.plain|$u"
    echo "before: $c $(geo "$c") | registry $(md "${ID[$c]}")"
    case "$(md "${ID[$c]}")" in "org.pz.plain|$c|"*) ;; *) stage_fail "the registry has no record of $c before the restart" ;; esac
done
[ "$(wprop k1 'w.keepAbove')" = true ] || stage_fail "k1 is not kept above"
[ "$(wprop f1 'w.fullScreen')" = true ] || stage_fail "f1 is not fullscreen"
[ "$(wprop s1 'w.onAllDesktops')" = true ] || stage_fail "s1 is not on all desktops"
S1BEFORE=$(rect "$(geo s1)")
MON="$NEST/e18a-monitor.log"
mon_start "$MON" "type='method_call',interface='org.plasmazones.WindowTracking',member='setWindowSticky'" \
    "type='method_call',interface='org.plasmazones.WindowTracking',member='setWindowMetadata'"
dkill
echo "nested daemon stopped"
sleep 1
mmark "$MON"
emark
drestart
for _ in $(seq 40); do
    elog "Bridge registered" | grep -q . && break
    sleep 0.5
done
elog "Bridge registered" | grep -q . || stage_fail "the effect never re-registered with the restarted daemon"
sleep 4
echo "bring-up calls in order:"
msgs "$MON" | sed 's/^/  /'
for c in n1 k1 f1 s1; do
    check "$c's record after the restart" "$(md "${ID[$c]}")" '~' "^org[.]pz[.]plain[|]$c[|]"
done
# stickyahead UUID -> the sticky value of the setWindowSticky line just before UUID's first
# setWindowMetadata line, or "none" when that line is not a setWindowSticky naming UUID
stickyahead() {
    msgs "$MON" | awk -v u="$1" '
        $1 == "setWindowMetadata" && index($2, u) { print (prev ~ ("^setWindowSticky [^ ]*" u) ? pv : "none"); found = 1; exit }
        { prev = $0; pv = $NF }
        END { if (!found) print "none" }'
}
for c in n1 k1 f1; do
    check "the bring-up's sticky state just ahead of $c's metadata" "$(stickyahead "${ID[$c]##*|}")" = false
done
check "the bring-up's sticky state just ahead of s1's metadata" "$(stickyahead "${ID[s1]##*|}")" = true
check "s1's frame after the restart" "$(rect "$(geo s1)")" = "$S1BEFORE"
finish
