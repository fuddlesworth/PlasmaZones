#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: GPL-3.0-or-later
#
# f763-snap-tile-follow-desktops: snap zone stores and autotile states follow their desktops
# through a desktop insert, a reorder and a removal (F763 snap and autotile arms, F787).
# Runner: 1 output, flag S.
# Staging: three desktops; desktops 1 and 2 COLS3 (snapping), desktop 3 autotile:bsp, Snap
#   Assist off. Desktop 1: s1 in zone 1; desktop 2: s2a in zone 1, s2b in zone 3; desktop 3: t1
#   and t2 tiled. Then KWin inserts a desktop at the front, swaps desktops 3 and 4, and removes
#   the inserted desktop. After each step every desktop is shown and read: its mode, the windows
#   on it, their zones, and the tiles when it runs autotile.
# Asserts: each desktop keeps its own windows, zones and tiles through all three steps; the
#   insert and the swap log the daemon's "Virtual desktops renumbered: N -> M desktops; moved
#   the rules, every engine's per-desktop state" with the snap and autotile applyDesktopRemap
#   lines, the removal logs the renumber; no step logs "inserted or moved" or a release or
#   carry from reconcileDesktopMemberships.
# A FAIL means: snap or autotile per-desktop state kept its old numbering, so a desktop resolved
#   another desktop's store and lost its zones or tiles.
set -u
NEST="$1"
LOG="$2"
. "$(dirname "$0")/../lib.sh"
kwjs_ok
for n in 1 2; do
    newdesk "$n" "D$((n + 1))"
    sleep 0.5
done
setting snapAssistEnabled false
assignd Virtual-0 1 "$COLS3"
assignd Virtual-0 2 "$COLS3"
assignd Virtual-0 3 "autotile:bsp"
sleep 2
desk 1
open s1
desk 2
open s2a
open s2b
desk 3
open t1
open t2
ids
declare -A ID
for c in s1 s2a s2b t1 t2; do
    u=$(uuid "$c")
    [ -n "$u" ] || stage_fail "$c never mapped"
    ID[$c]="org.pz.plain|$u"
done
Z1=$(zoneid "$COLS3" 1)
Z2=$(zoneid "$COLS3" 2)
Z3=$(zoneid "$COLS3" 3)
[ -n "$Z1" ] && [ -n "$Z2" ] && [ -n "$Z3" ] || stage_fail "could not read COLS3's zone ids"
desk 1
PZ org.plasmazones.Control.snapWindowToZone "${ID[s1]}" 1 Virtual-0 >/dev/null
sleep 1.5
desk 2
PZ org.plasmazones.Control.snapWindowToZone "${ID[s2a]}" 1 Virtual-0 >/dev/null
sleep 1
PZ org.plasmazones.Control.snapWindowToZone "${ID[s2b]}" 3 Virtual-0 >/dev/null
sleep 1.5
zname() {
    case "$1" in
    "$Z1") echo z1 ;;
    "$Z2") echo z2 ;;
    "$Z3") echo z3 ;;
    '') echo none ;;
    *) echo "?${1:0:10}" ;;
    esac
}
view() {
    jsread F763V '(function () { var cur = workspace.currentDesktop; var o = []; const w = workspace.windowList(); for (var i = 0; i < w.length; i++) { var c = w[i].caption; if (!/^(s1|s2a|s2b|t1|t2)$/.test(c)) { continue; } if (w[i].onAllDesktops || w[i].desktops.some(function (d) { return d.id === cur.id; })) { o.push(c); } } o.sort(); return o.join(" "); })()'
}
# desc D -> "m=MODE w=WINDOWS z=ZONES[ t=TILES]" for desktop D, shown first; TILES only on autotile
desc() {
    local m v zs="" c t=""
    desk "$1"
    m=$(modeof Virtual-0 "$1")
    v=$(view)
    for c in s1 s2a s2b; do
        case " $v " in *" $c "*) zs="$zs $c=$(zname "$(zoneof "${ID[$c]}")")" ;; esac
    done
    [ "$m" = 1 ] && t=" t=$(tileorder Virtual-0 | tr ' ' '\n' | sort | paste -sd' ')"
    echo "m=$m w=$v z=${zs# }$t"
}
EMPTY="m=0 w= z="
S1="m=0 w=s1 z=s1=z1"
S2="m=0 w=s2a s2b z=s2a=z1 s2b=z3"
TT="m=1 w=t1 t2 z= t=t1 t2"
# survey STEP WANT1 WANT2 ...: check every desktop against WANT1, WANT2 ...
survey() {
    local step="$1" d=1 got
    shift
    dmark
    for want in "$@"; do
        got=$(desc "$d")
        echo "  desktop $d: $got"
        check "$step: desktop $d" "$got" = "$want"
        d=$((d + 1))
    done
    check "$step: reconcile lines while the desktops were shown" \
        "$(dlog "reconcileDesktopMemberships: (released|moving|no equivalent)|was held on desktop|inserted or moved" | wc -l)" -eq 0
}
RECONCILE="reconcileDesktopMemberships: (released|moving|no equivalent)"
echo "start:"
d1=$(desc 1)
d2=$(desc 2)
d3=$(desc 3)
printf '  desktop 1: %s\n  desktop 2: %s\n  desktop 3: %s\n' "$d1" "$d2" "$d3"
[ "$d1" = "$S1" ] || stage_fail "desktop 1 is not s1 in zone 1 ($d1)"
[ "$d2" = "$S2" ] || stage_fail "desktop 2 is not s2a in zone 1 and s2b in zone 3 ($d2)"
[ "$d3" = "$TT" ] || stage_fail "desktop 3 does not tile t1 and t2 ($d3)"

echo "== insert a desktop at the front"
dmark
qdbus6 org.kde.KWin /VirtualDesktopManager org.kde.KWin.VirtualDesktopManager.createDesktop 0 X >/dev/null
sleep 3
check "insert: daemon renumber line" "$(dlog "Virtual desktops renumbered: 3 -> 4 desktops; moved the rules, every engine" | wc -l)" -ge 1
check "insert: snap remap line" "$(dlog "applyDesktopRemap: re-keyed [0-9]+ store\(s\) for a desktop list change" | wc -l)" -ge 1
check "insert: autotile remap line" "$(dlog "applyDesktopRemap: re-keyed states for a desktop list change" | wc -l)" -ge 1
check "insert: 'inserted or moved' lines" "$(dlog "inserted or moved" | wc -l)" -eq 0
check "insert: reconcile release or carry lines" "$(dlog "$RECONCILE" | wc -l)" -eq 0
survey insert "$EMPTY" "$S1" "$S2" "$TT"

echo "== swap desktops 3 and 4"
dmark
js 'workspace.moveDesktop(workspace.desktops[3], 2);'
sleep 3
check "swap: daemon renumber line" "$(dlog "Virtual desktops renumbered: 4 -> 4 desktops; moved the rules, every engine" | wc -l)" -ge 1
check "swap: snap remap line" "$(dlog "applyDesktopRemap: re-keyed [0-9]+ store\(s\) for a desktop list change" | wc -l)" -ge 1
check "swap: autotile remap line" "$(dlog "applyDesktopRemap: re-keyed states for a desktop list change" | wc -l)" -ge 1
check "swap: 'inserted or moved' lines" "$(dlog "inserted or moved" | wc -l)" -eq 0
check "swap: reconcile release or carry lines" "$(dlog "$RECONCILE" | wc -l)" -eq 0
survey swap "$EMPTY" "$S1" "$TT" "$S2"

echo "== remove the inserted desktop (now 1)"
ID1=$(deskid 1)
[ -n "$ID1" ] || stage_fail "could not read desktop 1's id"
dmark
qdbus6 org.kde.KWin /VirtualDesktopManager org.kde.KWin.VirtualDesktopManager.removeDesktop "$ID1" >/dev/null
sleep 3
check "removal: daemon renumber line" "$(dlog "Virtual desktops renumbered: 4 -> 3 desktops" | wc -l)" -ge 1
check "removal: reconcile release or carry lines" "$(dlog "$RECONCILE" | wc -l)" -eq 0
survey removal "$S1" "$TT" "$S2"
finish
