#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: GPL-3.0-or-later
#
# f1013-superseded-restore-ack: a client's ack of a superseded Restore configure is not read as the
# user un-maximizing a Monocle member. Runner: 2 outputs, no flags.
# Staging: Virtual-1 autotile:monocle with m2 then m1, both max=3 there. Six tries of
#   kscreen-doctor disable + enable of Virtual-1 (m1 and m2 evacuate to Virtual-0 and come back).
#   The race (a Restore batch and a Full batch inside one client round trip, so the client acks
#   the superseded Restore) fires in some tries, seen as a "Re-issuing ... over a superseded
#   configure's ack" line or one of the two Monocle lines below. A try that leaves a member
#   floating or not max=3 is re-staged (Meta+F, Tiling.retile) before the next. Then the user's own
#   setMaximize(false, false) on m1.
# Asserts: no try logs "Monocle window manually unmaximized" without a user step, and every try
#   ends with m1 and m2 max=3 on Virtual-1 and not floating; the user's un-maximize logs one
#   "Monocle window manually unmaximized" for m1 and floats it. STAGING-FAILED when the race never
#   fired in the six tries.
# A FAIL means: the stale ack was taken for a user un-maximize, that member floated and the other
#   was left a lone non-monocle tile.
set -u
NEST="$1"
LOG="$2"
. "$(dirname "$0")/../lib.sh"
kwjs_ok
need_outputs 2
both() { echo "m1 $(frame m1) | m2 $(frame m2)"; }
staged() {
    case "$(both)" in
    "m1 "*"@Virtual-1 max=3 | m2 "*"@Virtual-1 max=3") return 0 ;;
    esac
    return 1
}
assign Virtual-1 "autotile:monocle"
sleep 2
for c in m2 m1; do
    open "$c"
    sendto "$c" Virtual-1 || stage_fail "$c not on Virtual-1 ($(geo "$c"))"
    sleep 0.5
done
ids
M1="org.pz.plain|$(uuid m1)"
M2="org.pz.plain|$(uuid m2)"
echo "staged: $(both)"
staged || stage_fail "no monocle: $(both)"
FIRED=0
for try in 1 2 3 4 5 6; do
    echo "== try $try"
    emark
    ksd output.Virtual-1.disable
    sleep 4
    echo "  evacuated: $(both)"
    ksd output.Virtual-1.enable
    sleep 5
    RI=$(elog "Re-issuing .* superseded configure's ack.*(${M1##*|}|${M2##*|})" | wc -l)
    MU=$(elog "Monocle window manually unmaximized" | wc -l)
    SR=$(elog "Monocle window committed a superseded restore" | wc -l)
    [ $((RI + MU + SR)) -gt 0 ] && FIRED=$((FIRED + 1))
    elog "Re-issuing .*|Monocle window manually unmaximized.*|Monocle window committed a superseded restore.*" | head -4 | sed 's/^/    /'
    F1=$(floating "$M1")
    F2=$(floating "$M2")
    S=$(both)
    check "try $try: 'Monocle window manually unmaximized' lines with no user step" "$MU" -eq 0
    check "try $try: m1 and m2 Monocle-maximized on Virtual-1" "$(yesno staged)" = yes
    check "try $try: m1 and m2 not floating" "$F1 $F2" = "false false"
    if [ "$F1" = true ] || [ "$F2" = true ] || ! staged; then
        for c in m1 m2; do
            id="$M1"
            [ "$c" = m2 ] && id="$M2"
            if [ "$(floating "$id")" = true ]; then
                activate "$c"
                shortcut toggle_window_float
                sleep 2
            fi
        done
        PZ org.plasmazones.Tiling.retile Virtual-1 >/dev/null
        sleep 2.5
        echo "  re-staged: $(both)"
        staged || {
            note "re-staging failed after try $try: $S"
            break
        }
    fi
done
[ "$FIRED" -gt 0 ] || stage_fail "the superseded-ack race never fired in the tries"
echo "race fired in $FIRED tries"
echo "== the user's own un-maximize of m1"
emark
activate m1
on m1 'w[i].setMaximize(false, false);'
sleep 1.5
echo "  after: $(both)"
check "'Monocle window manually unmaximized' lines for m1 after the user's un-maximize" \
    "$(elog "Monocle window manually unmaximized.*${M1##*|}" | wc -l)" -eq 1
check "m1 floating after the user's un-maximize" "$(floating "$M1")" = true
finish
