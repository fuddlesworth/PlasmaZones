#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: GPL-3.0-or-later
#
# f963-minimized-reslot: a window minimized through a layout switch takes the same-numbered zone of
# the new layout when it is unminimized. Runner: 1 output, flag S.
# Staging: snapping, Snap Assist off, snapUnfloatFallbackToZone off. Layouts A COLS3, B Split Focus
#   (zone 2 the top-right quarter, zone 3 the bottom-right quarter) and C Columns (2) (no zone 3);
#   the monitor runs A. p1, p2, p3 are placed free (p1 at 100,500) and snapped: p1 zone 2, p2 zone
#   1, p3 zone 3. Layouts switch through LayoutRegistry.assignLayoutToScreen (the quick-layout
#   pass; the shortcut does not reach the nested KGlobalAccel).
#   1-3. p1 is minimized (KWin's own minimize), the monitor switches to B, p1 is unminimized.
#   4. p1 is snapped into B's zone 3, minimized, switched to C, unminimized.
#   5. control: p3 is snapped into C's zone 2 and floated with the float shortcut, switched to A,
#   the float shortcut again.
# Asserts: the switch to B logs "Re-slotted the pre-float zones of 1 minimized window(s)", p2 lands
#   in B's zone 1, and p1 comes back not floating in B's zone 2; step 4 brings p1 back floating with
#   no zone; step 5 leaves p3 floating with no zone.
# A FAIL means: p1 came back floating over B instead of taking B's zone 2.
set -u
NEST="$1"
LOG="$2"
. "$(dirname "$0")/../lib.sh"
kwjs_ok
LA="$COLS3"
LB="{a11899b1-f0e3-4425-9363-acb71726c566}"
LC="$COLS2"
setting snapAssistEnabled false
setting snapUnfloatFallbackToZone false
[ "$(PZ org.plasmazones.Settings.getSetting snapUnfloatFallbackToZone)" = false ] \
    || stage_fail "snapUnfloatFallbackToZone did not switch off"
assign Virtual-0 "$LA"
sleep 2
layoutnow() {
    case "$(PZ org.plasmazones.LayoutRegistry.getLayoutForScreen Virtual-0 | head -1)" in
    "$LA") echo A ;;
    "$LB") echo B ;;
    "$LC") echo C ;;
    *) echo other ;;
    esac
}
minimize() {
    on "$1" "w[i].minimized = $2;"
    sleep 2
}
switchto() {
    dmark
    assign Virtual-0 "$1"
    sleep 2.5
    echo "  layout now: $(layoutnow)"
}
# lbl ZONEID: the zone as layout letter and zone number
declare -A LBL
for L in A B C; do
    case "$L" in A) id="$LA" ;; B) id="$LB" ;; C) id="$LC" ;; esac
    for n in 1 2 3; do
        z=$(zoneid "$id" "$n")
        [ -n "$z" ] && LBL[$z]="$L$n"
    done
done
[ "${#LBL[@]}" = 8 ] || stage_fail "expected 8 zones across A, B and C, read ${#LBL[@]}"
lbl() {
    if [ -z "$1" ]; then
        echo none
    else
        echo "${LBL[$1]:-other}"
    fi
}
for c in p1 p2 p3; do
    open "$c"
done
moveto p1 100 500
sleep 1
ids
declare -A ID
for c in p1 p2 p3; do
    u=$(uuid "$c")
    [ -n "$u" ] || stage_fail "$c never mapped"
    ID[$c]="org.pz.plain|$u"
done
zstate() { echo "$(geo "$1") floating=$(floating "${ID[$1]}") zone=$(lbl "$(zoneof "${ID[$1]}")")"; }
zl() { lbl "$(zoneof "${ID[$1]}")"; }
snap() {
    PZ org.plasmazones.Control.snapWindowToZone "${ID[$1]}" "$2" Virtual-0 >/dev/null
    sleep 1.5
}
snap p1 2
snap p2 1
snap p3 3
echo "on A: p1 $(zstate p1) | p2 $(zstate p2) | p3 $(zstate p3)"
[ "$(zl p1)" = A2 ] || stage_fail "p1 not in A's zone 2"
echo "== steps 1-3: minimize p1, switch to B, unminimize"
minimize p1 true
switchto "$LB"
[ "$(layoutnow)" = B ] || stage_fail "the switch to Split Focus did not take"
dlog "Re-slotted the pre-float zones.*" | sed 's/^/    /'
check "the switch to B: re-slot lines for 1 minimized window" \
    "$(dlog "Re-slotted the pre-float zones of 1 minimized window" | wc -l)" -eq 1
check "p2 after the switch: in B's zone 1" "$(zl p2)" = B1
minimize p1 false
echo "p1 unminimized: $(zstate p1)"
check "p1 unminimized on B: not floating" "$(floating "${ID[p1]}")" = false
check "p1 unminimized on B: in B's zone 2" "$(zl p1)" = B2
echo "== step 4: p1 in B's zone 3, minimized, switch to C (no zone 3), unminimize"
snap p1 3
[ "$(zl p1)" = B3 ] || stage_fail "p1 did not snap into B's zone 3"
minimize p1 true
switchto "$LC"
[ "$(layoutnow)" = C ] || stage_fail "the switch to Columns (2) did not take"
minimize p1 false
echo "p1 unminimized on C: $(zstate p1)"
check "p1 unminimized on C: floating" "$(floating "${ID[p1]}")" = true
check "p1 unminimized on C: no zone" "$(zl p1)" = none
echo "== step 5 (control): p3 floated on C, switch to A, float shortcut again"
snap p3 2
[ "$(zl p3)" = C2 ] || stage_fail "p3 did not snap into C's zone 2"
floatkey p3
[ "$(floating "${ID[p3]}")" = true ] || stage_fail "the float shortcut did not float p3"
switchto "$LA"
floatkey p3
echo "p3 after the second float shortcut on A: $(zstate p3)"
check "p3 after the second float shortcut on A: floating" "$(floating "${ID[p3]}")" = true
check "p3 after the second float shortcut on A: no zone" "$(zl p3)" = none
finish
