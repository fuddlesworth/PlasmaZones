#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: GPL-3.0-or-later
#
# f1101-desktop-names-relay: the daemon relays desktop names, so settings hears a desktop added or
# renamed. Runner: 1 output, no flags.
# Staging: the session's one desktop and a dbus-monitor of every LayoutRegistry signal. Desktop D2
#   then D3 are created through KWin's VirtualDesktopManager, then D3 is renamed R3 from a KWin
#   script.
# Asserts: after each create a virtualDesktopNamesChanged carrying the new name (D2, then D2 and
#   D3), after the rename one carrying R3.
# A FAIL means: only virtualDesktopCountChanged went out, so the settings desktop list went stale.
set -u
NEST="$1"
LOG="$2"
. "$(dirname "$0")/../lib.sh"
kwjs_ok
MON="$NEST/f1101-monitor.log"
mon_start "$MON" "type='signal',interface='org.plasmazones.LayoutRegistry'"
names() { PZ org.plasmazones.LayoutRegistry.getVirtualDesktopNames | tr '\n' ',' | sed 's/,$//'; }
desksigs() { msgs "$MON" | grep "^virtualDesktop" | sed 's/^/    /'; }
namesigs() { msgs "$MON" | grep "^virtualDesktopNamesChanged " | grep -c "$1"; }
echo "names at start: $(names)"
echo "== desktop D2 created"
mmark "$MON"
newdesk 1 D2
sleep 2
desksigs
echo "  getVirtualDesktopNames: $(names)"
check "virtualDesktopNamesChanged naming D2" "$(namesigs D2)" -ge 1
echo "== desktop D3 created"
mmark "$MON"
newdesk 2 D3
sleep 2
desksigs
echo "  getVirtualDesktopNames: $(names)"
check "virtualDesktopNamesChanged naming D2 and D3" \
    "$(msgs "$MON" | grep "^virtualDesktopNamesChanged " | grep "D2" | grep -c "D3")" -ge 1
echo "== D3 renamed R3"
mmark "$MON"
js 'workspace.desktops[2].name = "R3";'
sleep 2
KN=$(jsread DNAMES 'workspace.desktops.map(function (d) { return d.name; }).join(",")')
echo "  KWin's names: $KN"
case "$KN" in *R3*) ;; *) stage_fail "KWin did not take the rename ($KN)" ;; esac
desksigs
echo "  getVirtualDesktopNames: $(names)"
check "virtualDesktopNamesChanged naming R3" "$(namesigs R3)" -ge 1
finish
