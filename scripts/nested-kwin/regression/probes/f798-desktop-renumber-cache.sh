#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: GPL-3.0-or-later
#
# f798-desktop-renumber-cache: the effect's per-output desktop cache follows KWin's renumbering
# after a desktop removal. Runner: 1 output, no flags.
# Staging: four desktops (global), desktop 3 shown. Desktop 1 is removed: KWin renumbers and the
#   shown desktop becomes 2 with no current-desktop signal. Then desktop 3 is shown.
# Asserts: after the switch the effect sends WindowTracking.screenDesktopChanged naming desktop 3,
#   and the daemon's current desktop reads 3.
# A FAIL means: the cache still held 3 from before the removal, the dedup dropped the report and
#   the daemon kept desktop 2.
set -u
NEST="$1"
LOG="$2"
. "$(dirname "$0")/../lib.sh"
kwjs_ok
for n in 1 2 3; do
    newdesk "$n" "D$n"
done
sleep 0.5
js 'workspace.currentDesktop = workspace.desktops[2];'
sleep 1.5
[ "$(curdesk)" = "3 of 4" ] || stage_fail "desktop 3 of 4 is not in view ($(curdesk))"
MON="$NEST/f798-monitor.log"
mon_start "$MON" "type='method_call',interface='org.plasmazones.WindowTracking',member='screenDesktopChanged'"
FIRST=$(deskid 1)
[ -n "$FIRST" ] || stage_fail "no id for desktop 1"
echo "removing desktop 1 ($FIRST)"
qdbus6 org.kde.KWin /VirtualDesktopManager org.kde.KWin.VirtualDesktopManager.removeDesktop "$FIRST" >/dev/null
sleep 1.5
echo "after the removal: $(curdesk)"
[ "$(curdesk)" = "2 of 3" ] || stage_fail "KWin did not renumber the shown desktop to 2 of 3 ($(curdesk))"
mmark "$MON"
js 'workspace.currentDesktop = workspace.desktops[2];'
sleep 1.5
echo "after the switch right: $(curdesk)"
[ "$(curdesk)" = "3 of 3" ] || stage_fail "the switch did not show desktop 3 ($(curdesk))"
sleep 1
echo "screenDesktopChanged sends since the switch:"
msgs "$MON" | grep "^screenDesktopChanged " | sed 's/^/    /'
check "screenDesktopChanged sends naming desktop 3 after the switch" \
    "$(msgs "$MON" | awk '$1 == "screenDesktopChanged" && $NF == 3' | wc -l)" -ge 1
check "the daemon's current desktop" "$(PZ org.plasmazones.LayoutRegistry.getCurrentVirtualDesktop | head -1)" = 3
finish
