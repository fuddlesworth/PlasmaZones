#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: GPL-3.0-or-later
#
# f639-floating-evacuee-unplug: a snap-floating window KWin evacuates off an unplugged monitor stays
# floating through the daemon's unplug prune. Runner: 2 outputs, no flags.
# Staging: both outputs COLS3, Snap Assist off. p1 is snapped into Virtual-1's zone 1 and floated
#   with the float shortcut; p2 sits snapped in Virtual-1's zone 3. Five tries: Virtual-1 is
#   disabled with kscreen-doctor (KWin moves p1 and p2 onto Virtual-0) while isWindowFloating p1 is
#   polled every 0.1 s, then enabled again. After the last try the float shortcut is pressed on p1
#   on Virtual-0.
# Asserts: every try reads p1 floating throughout the poll, logs "SnapEngine::handoffReceive: <p1>
#   ... wasFloating= true" and leaves p1 floating on Virtual-0 with no zone; every enable brings p1
#   back floating on Virtual-1; the float shortcut on Virtual-0 is answered by snap (its feedback is
#   not "not_managed").
# A FAIL means: the prune ran before the settle and p1 read not floating, or snap no longer managed
#   it on Virtual-0.
set -u
NEST="$1"
LOG="$2"
. "$(dirname "$0")/../lib.sh"
kwjs_ok
need_outputs 2
on_exit 'ksd output.Virtual-1.enable >/dev/null'
setting snapAssistEnabled false
for s in Virtual-0 Virtual-1; do
    assign "$s" "$COLS3"
done
sleep 2
for c in p1 p2; do
    open "$c" 300 300
    sendto "$c" Virtual-1 || stage_fail "$c not on Virtual-1 ($(geo "$c"))"
done
ids
P1U=$(uuid p1)
P2U=$(uuid p2)
[ -n "$P1U" ] && [ -n "$P2U" ] || stage_fail "p1 or p2 never mapped"
P1="org.pz.plain|$P1U"
P2="org.pz.plain|$P2U"
PZ org.plasmazones.Control.snapWindowToZone "$P2" 3 Virtual-1 >/dev/null
PZ org.plasmazones.Control.snapWindowToZone "$P1" 1 Virtual-1 >/dev/null
sleep 1.5
echo "p1 snapped in Virtual-1 zone 1: $(wstat p1 "$P1")"
activate p1
shortcut toggle_window_float
sleep 2
S=$(wstat p1 "$P1")
echo "p1 floated on Virtual-1: $S | p2 $(wstat p2 "$P2")"
case "$S" in *"@Virtual-1 floating=true zone=''") ;; *) stage_fail "p1 not floating on Virtual-1" ;; esac
TRIES=5
for try in $(seq 1 "$TRIES"); do
    echo "== try $try"
    dmark
    POLL="$NEST/f639-poll-$try.txt"
    : > "$POLL"
    (
        for _ in $(seq 40); do
            echo "$(floating "$P1" | tr -d '\n' | cut -c1-8)" >> "$POLL"
            sleep 0.1
        done
    ) &
    POLLPID=$!
    ksd output.Virtual-1.disable
    wait "$POLLPID"
    sleep 1
    echo "  floating timeline, runs of t(rue)/F(alse)/?(other) at 0.1 s: $(awk '{ v = ($0 == "true") ? "t" : (($0 == "false") ? "F" : "?"); if (v == p) { n++ } else { if (p != "") printf "%s%d ", p, n; p = v; n = 1 } } END { if (p != "") printf "%s%d", p, n }' "$POLL")"
    dlog "parkOutput:.*|reportOutputSettle: .*$P1U.*|handoffReceive: .*$P1U.*" | head -6 | sed 's/^/    /'
    check "try $try: reads of p1 not floating during the unplug" "$(grep -c '^false' "$POLL")" -eq 0
    check "try $try: snap handoffs of p1 with wasFloating= true" \
        "$(dlog "SnapEngine::handoffReceive: [^ ]*$P1U.*wasFloating= true" | wc -l)" -ge 1
    check "try $try: p1 after the settle" "$(wstat p1 "$P1")" '~' "@Virtual-0 floating=true zone=''$"
    if [ "$try" -lt "$TRIES" ]; then
        ksd output.Virtual-1.enable
        sleep 5
        echo "  back: p1 $(wstat p1 "$P1") | p2 $(wstat p2 "$P2")"
        check "try $try: p1 back floating on Virtual-1" "$(wstat p1 "$P1")" '~' "@Virtual-1 floating=true"
        case "$(wstat p1 "$P1")" in *"@Virtual-1 floating=true"*) ;; *) break ;; esac
    fi
done
MON="$NEST/f639-monitor.log"
mon_start "$MON" "type='signal',interface='org.plasmazones.WindowTracking',member='navigationFeedback'"
mmark "$MON"
activate p1
shortcut toggle_window_float
sleep 2
FB=$(msgs "$MON" | grep "^navigationFeedback " | head -1)
echo "float shortcut on p1 on Virtual-0, feedback: $FB | p1 $(wstat p1 "$P1")"
[ -n "$FB" ] || stage_fail "the float shortcut sent no navigationFeedback"
check "the float shortcut's feedback on Virtual-0" "$FB" '!~' "not_managed"
finish
