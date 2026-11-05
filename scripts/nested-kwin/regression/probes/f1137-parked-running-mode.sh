#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: GPL-3.0-or-later
#
# f1137-parked-running-mode: a parked context goes back by the mode it runs, not its raw assignment.
# Runner: 2 outputs, no flags; PZ_F1137_RUN=snap (row f1137-parked-running-mode-snap) or tile (row
#   f1137-parked-running-mode-tile).
# Staging: two desktops, per-output desktops off, Snap Assist off. Virtual-1 (A) goes away and comes
#   back untouched; Virtual-0 (B) COLS3 on both desktops. Desktop 1 is in view at the removal and the
#   return, so A's desktop 2 context is parked and returns out of view.
#   snap: autotileEnabled false; A desktop 2 is given COLS3 and then autotile:bsp (raw mode autotile,
#     running mode snapping). On desktop 2 w opens on A and is snapped into zone 1.
#   tile: tiling on, A autotile:bsp on both desktops; t1 and t2 tile on A desktop 2. While A is away
#     autotileDisabledDesktops is set to ['Virtual-1/2'], so A desktop 2 no longer tiles.
# Asserts: snap: SnapEngine::readoptParked and the daemon's readoptEvacuee re-seat w, and on desktop 2
#   w is in zone 1 again. tile: no AutotileEngine::readoptParked re-seat for t1 or t2, and no
#   windowsTileRequested batch names them after desktop 2 is shown.
# A FAIL means: snap: the raw autotile mode refused the context and w came back in no zone; tile: the
#   tiles were seated into a desktop that no longer tiles.
set -u
NEST="$1"
LOG="$2"
RUN="${PZ_F1137_RUN:-snap}"
. "$(dirname "$0")/../lib.sh"
kwjs_ok
need_outputs 2
case "$RUN" in snap | tile) ;; *) stage_fail "unknown PZ_F1137_RUN '$RUN'" ;; esac
on_exit 'ksd output.Virtual-1.enable >/dev/null; setting autotileEnabled true; gsetting autotileDisabledDesktops "<@as []>" >/dev/null'
setting snapAssistEnabled false
newdesk 1 D2
assignd Virtual-0 1 "$COLS3"
assignd Virtual-0 2 "$COLS3"
if [ "$RUN" = snap ]; then
    assignd Virtual-1 1 "$COLS3"
    assignd Virtual-1 2 "$COLS3"
    sleep 1
    setting autotileEnabled false
    sleep 1
    echo "autotileEnabled reads $(PZ org.plasmazones.Settings.getSetting autotileEnabled)"
    assignd Virtual-1 2 "autotile:bsp"
else
    assignd Virtual-1 1 "autotile:bsp"
    assignd Virtual-1 2 "autotile:bsp"
fi
sleep 1.5
echo "modes: A 1=$(modeof Virtual-1 1) 2=$(modeof Virtual-1 2) | B 1=$(modeof Virtual-0 1) 2=$(modeof Virtual-0 2) (0 snapping, 1 autotile)"
[ "$(modeof Virtual-1 2)" = 1 ] || stage_fail "A desktop 2 does not read the autotile mode ($(modeof Virtual-1 2))"
desk 2
if [ "$RUN" = snap ]; then
    open w
    sendto w Virtual-1 || stage_fail "w not on Virtual-1 ($(geo w))"
    sleep 1.5
    ids
    WU=$(uuid w)
    [ -n "$WU" ] || stage_fail "w never mapped"
    W="org.pz.plain|$WU"
    PZ org.plasmazones.Control.snapWindowToZone "$W" 1 Virtual-1 >/dev/null
    sleep 2
    Z=$(zoneof "$W")
    echo "staged: w (...${WU: -8}) $(geo w) zone=$Z floating=$(floating "$W")"
    case "$Z" in "{"*) ;; *) stage_fail "w did not snap on A desktop 2 (zone '$Z'); the context offers no zone to park" ;; esac
    UUIDS="$WU"
else
    for c in t1 t2; do
        open "$c"
        sendto "$c" Virtual-1 || stage_fail "$c not on Virtual-1 ($(geo "$c"))"
    done
    sleep 2
    ids
    T1U=$(uuid t1)
    T2U=$(uuid t2)
    [ -n "$T1U" ] && [ -n "$T2U" ] || stage_fail "t1 or t2 never mapped"
    echo "staged: t1 $(geo t1) | t2 $(geo t2) | A tiles $(tileorder Virtual-1)"
    case "$(tiles Virtual-1)" in 2*) ;; *) stage_fail "A desktop 2 does not hold two tiles" ;; esac
    UUIDS="$T1U $T2U"
fi
desk 1
echo "desktop $(curdesk) shown"
MON="$NEST/f1137-monitor.log"
mon_start "$MON" "type='signal',interface='org.plasmazones.Tiling',member='windowsTileRequested'"
echo "== Virtual-1 disabled"
dmark
ksd output.Virtual-1.disable
sleep 4
for u in $UUIDS; do settlerows "$u"; done
if [ "$RUN" = tile ]; then
    echo "  autotileDisabledDesktops ['Virtual-1/2']: $(gsetting autotileDisabledDesktops "<['Virtual-1/2']>")"
    sleep 2
fi
echo "== Virtual-1 enabled again, desktop 1 in view, nothing touched"
dmark
ksd output.Virtual-1.enable
sleep 6
for u in $UUIDS; do settlerows "$u"; done
if [ "$RUN" = snap ]; then
    echo "  w $(geo w)"
    check "SnapEngine::readoptParked re-seat lines for w" "$(reseated Snap "$WU")" -eq 1
    check "daemon readoptEvacuee re-seated lines for w" \
        "$(dlog "readoptEvacuee: [^ ]*$WU[^ ]* re-seated on" | wc -l)" -ge 1
    echo "== desktop 2 shown"
    desk 2
    sleep 2
    echo "  desktop $(curdesk): w $(geo w)"
    check "w's zone on desktop 2" "$(zoneof "$W")" = "$Z"
else
    echo "  t1 $(geo t1) | t2 $(geo t2)"
    check "AutotileEngine::readoptParked re-seat lines for t1" "$(reseated Autotile "$T1U")" -eq 0
    check "AutotileEngine::readoptParked re-seat lines for t2" "$(reseated Autotile "$T2U")" -eq 0
    echo "== desktop 2 shown"
    mmark "$MON"
    desk 2
    sleep 2.5
    echo "  desktop $(curdesk): t1 $(geo t1) | t2 $(geo t2) | A mode on desktop 2 (raw) $(modeof Virtual-1 2)"
    check "windowsTileRequested batches naming t1 since the switch" "$(named "$MON" windowsTileRequested "$T1U")" -eq 0
    check "windowsTileRequested batches naming t2 since the switch" "$(named "$MON" windowsTileRequested "$T2U")" -eq 0
fi
finish
