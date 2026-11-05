#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: GPL-3.0-or-later
#
# f800-removed-desktop-retile: tiles KWin moves off a removed desktop in view stay tracked and are
# tiled again where they land. Runner: 1 output, no flags.
# Staging: three desktops, desktops 2 and 3 autotile:bsp on Virtual-0, t1 and t2 tiled on desktop 2
#   with the user on it. A dbus-monitor records the Tiling interface. Desktop 2 is removed, so KWin
#   moves t1 and t2 onto the old desktop 3 (the new 2, in view); then t1 is activated.
# Asserts: the daemon logs "readoptRenumberEvictees: <id> left a removed desktop for the one in view
#   on Virtual-0" for t1 and t2, before "Virtual desktops renumbered: 3 -> 2"; window-state rows for
#   both exist right after the removal; the last tiling verdict on the bus naming t1 is
#   windowsTileRequested; activating t1 logs no "window not tracked"; both are in Virtual-0's tiles.
# A FAIL means: the renumber's prune released the re-announced tiles and nothing tiled them again.
set -u
NEST="$1"
LOG="$2"
. "$(dirname "$0")/../lib.sh"
kwjs_ok
newdesk 1 D2
newdesk 2 D3
sleep 1.5
assignd Virtual-0 2 "autotile:bsp"
assignd Virtual-0 3 "autotile:bsp"
sleep 1.5
desk 2
open t1 300 300
open t2 300 300
ids
T1=$(uuid t1)
T2=$(uuid t2)
[ -n "$T1" ] && [ -n "$T2" ] || stage_fail "t1 or t2 never mapped"
rows() {
    gdbus call --session --dest org.plasmazones --object-path /PlasmaZones \
        --method org.plasmazones.WindowTracking.getAllWindowStates 2>&1 | tr '(' '\n' | grep -cE "$T1|$T2"
}
TL0=$(tiles Virtual-0)
echo "tiled on desktop 2 ($(curdesk)): $(geo t1) | $(geo t2) | tiles $TL0"
case "$TL0" in "2 "*"'${T1: -8}'"*) ;; *) stage_fail "t1/t2 not both tiled on desktop 2 ($TL0)" ;; esac
case "$TL0" in *"'${T2: -8}'"*) ;; *) stage_fail "t2 not tiled on desktop 2 ($TL0)" ;; esac
MON="$NEST/d5-monitor.log"
mon_start "$MON" "interface='org.plasmazones.Tiling'"
mmark "$MON"
dmark
IDRM=$(deskid 2)
[ -n "$IDRM" ] || stage_fail "no id for desktop 2"
echo "removing desktop 2 ($IDRM)"
qdbus6 org.kde.KWin /VirtualDesktopManager org.kde.KWin.VirtualDesktopManager.removeDesktop "$IDRM" >/dev/null
sleep 3
echo "after the removal ($(curdesk)): $(geo t1) | $(geo t2)"
check "window-state rows for t1 and t2 right after the removal" "$(rows)" = 2
for c in t1 t2; do
    u=$([ "$c" = t1 ] && echo "$T1" || echo "$T2")
    check "$c is re-adopted from the removed desktop" \
        "$(dlog "readoptRenumberEvictees: [^ ]*$u[^ ]* left a removed desktop for the one in view on \"Virtual-0\"" | wc -l)" -ge 1
done
RD=$(tail -n +"$DMARK" "$NEST/daemon.log" | grep -n "readoptRenumberEvictees: .* left a removed desktop" | tail -1 | cut -d: -f1)
RN=$(tail -n +"$DMARK" "$NEST/daemon.log" | grep -n "Virtual desktops renumbered: 3 -> 2" | head -1 | cut -d: -f1)
[ -n "$RN" ] || stage_fail "the daemon logged no renumber for the removal"
check "the re-adoption runs before the renumber completes" "${RD:-999999}" -lt "$RN"
dmark
activate t1
sleep 1
check "activating t1 finds it tracked" "$(dlog "window not tracked.*$T1" | wc -l)" = 0
TL=$(tiles Virtual-0)
echo "tiles on Virtual-0: $TL"
check "t1 is in Virtual-0's tiles" "$TL" '~' "'${T1: -8}'"
check "t2 is in Virtual-0's tiles" "$TL" '~' "'${T2: -8}'"
check "the last tiling verdict naming t1 is windowsTileRequested" \
    "$(msgs "$MON" | grep "$T1" | grep -oE "^(windowsTileRequested|windowsReleasedFromTiling)" | tail -1)" = windowsTileRequested
finish
