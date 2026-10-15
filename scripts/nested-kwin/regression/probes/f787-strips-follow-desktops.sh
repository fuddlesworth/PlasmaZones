#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: GPL-3.0-or-later
#
# f787-strips-follow-desktops: scrolling strips follow their desktops through a desktop insert, a
# reorder and a removal (F763 scroll arm, F787). Runner: 1 output, no flags.
# Staging: three desktops, scrolling on all. Desktop 1: a; desktop 2: b1, b2 (b2 widened to
#   0.8); desktop 3: c. Then KWin inserts a desktop at the front (old 1..3 become 2..4), moves
#   desktop 3 to the end (b and c swap), and removes desktop 2 (a's). After each step every
#   desktop is shown and its strip read.
# Asserts: after the insert 1 is empty, 2 [a], 3 [b1][b2 wide], 4 [c]; after the reorder 3 [c]
#   and 4 [b1][b2 wide]; after the removal 2 [c] and 3 [b1][b2 wide]; no release line at the
#   insert or the reorder, and none naming b1, b2 or c at the removal.
# A FAIL means: a strip stayed keyed to the desktop number it had, so it showed on the wrong
#   desktop or its windows were released as having left.
set -u
NEST="$1"
LOG="$2"
. "$(dirname "$0")/../lib.sh"
kwjs_ok
for n in 1 2; do
    newdesk "$n" "D$((n + 1))"
    sleep 0.5
done
assign Virtual-0 "scrolling:"
sleep 2
desk 1
open a
desk 2
open b1
open b2
activate b2
sleep 0.4
PZ org.plasmazones.Scrolling.setColumnWidthProportion Virtual-0 0.8 >/dev/null
sleep 1.5
desk 3
open c
ids
kwdesks() {
    jsread P23K '(function () { const w = workspace.windowList(); var o = []; for (var i = 0; i < w.length; i++) { if (/^(a|b1|b2|c)$/.test(w[i].caption)) { o.push(w[i].caption + "=" + w[i].desktops.map(function (d) { return d.x11DesktopNumber; })); } } o.sort(); return o.join(" ") + " of " + workspace.desktops.length; })()'
}
declare -A STRIP
# readstrips: show every desktop in turn and fill STRIP[N] with its strip
readstrips() {
    local k n d
    k=$(kwdesks)
    echo "  KWin: $k"
    n=${k##* of }
    case "$n" in '' | *[!0-9]*) stage_fail "could not read KWin's desktops ($k)" ;; esac
    STRIP=()
    for d in $(seq 1 "$n"); do
        desk "$d"
        STRIP[$d]=$(stripx)
        echo "  desktop $d strip: ${STRIP[$d]}"
    done
}
echo "start:"
readstrips
D=$(echo "${STRIP[1]}" | grep -o "\[a [0-9]*\]" | tr -d '[]' | awk '{ print $2 }')
W=$(echo "${STRIP[2]}" | grep -o "\[b2 [0-9]*\]" | tr -d '[]' | awk '{ print $2 }')
[ -n "$D" ] && [ "${STRIP[1]}" = "[a $D]" ] || stage_fail "desktop 1's strip is not [a] (${STRIP[1]})"
[ "${STRIP[2]}" = "[b1 $D] [b2 ${W:-?}]" ] && [ "$W" -gt "$D" ] || stage_fail "desktop 2's strip is not [b1][b2 wide] (${STRIP[2]})"
[ "${STRIP[3]}" = "[c $D]" ] || stage_fail "desktop 3's strip is not [c] (${STRIP[3]})"
B="[b1 $D] [b2 $W]"

echo "== insert a desktop at the front"
dmark
qdbus6 org.kde.KWin /VirtualDesktopManager org.kde.KWin.VirtualDesktopManager.createDesktop 0 X >/dev/null
sleep 3
readstrips
check "insert: desktop 1's strip" "${STRIP[1]}" = "(no strip)"
check "insert: desktop 2's strip" "${STRIP[2]}" = "[a $D]"
check "insert: desktop 3's strip" "${STRIP[3]}" = "$B"
check "insert: desktop 4's strip" "${STRIP[4]:-}" = "[c $D]"
check "insert: release lines" "$(dlog "reconcileDesktopMemberships: released" | wc -l)" -eq 0
note "remap lines: $(dlog "applyDesktopRemap: re-keyed strips" | wc -l)"

echo "== move desktop 3 to the end"
dmark
js 'workspace.moveDesktop(workspace.desktops[2], 3);'
sleep 3
readstrips
check "reorder: desktop 3's strip" "${STRIP[3]}" = "[c $D]"
check "reorder: desktop 4's strip" "${STRIP[4]:-}" = "$B"
check "reorder: release lines" "$(dlog "reconcileDesktopMemberships: released" | wc -l)" -eq 0

echo "== remove desktop 2 (a's)"
desk 1
dmark
ID2=$(deskid 2)
[ -n "$ID2" ] || stage_fail "could not read desktop 2's id"
qdbus6 org.kde.KWin /VirtualDesktopManager org.kde.KWin.VirtualDesktopManager.removeDesktop "$ID2" >/dev/null
sleep 3
readstrips
check "removal: desktop 2's strip" "${STRIP[2]}" = "[c $D]"
check "removal: desktop 3's strip" "${STRIP[3]:-}" = "$B"
# a's own desktop went, so only the windows above it are held to staying put.
check "removal: release lines naming b1, b2 or c" \
    "$(dlog "reconcileDesktopMemberships: released [^ ]*($(uuid b1)|$(uuid b2)|$(uuid c))" | wc -l)" -eq 0
note "a after the removal: $(geo a)"
finish
