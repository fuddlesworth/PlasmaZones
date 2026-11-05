#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: GPL-3.0-or-later
#
# f161-move-to-screen-redesk: a PlasmaZones move to another monitor re-desks the window onto the
# desktop that monitor shows, as KWin's own Move to Screen does. Runner: 2 outputs, flags N.
# Staging: two desktops, per-output desktops on, Virtual-0 shows desktop 1 (COLS3) and Virtual-1
#   desktop 2 (Columns (2)), Snap Assist off. p1 is snapped into COLS3 zone 1, then
#   Snap.moveWindowToZone moves it into Columns (2) zone 1 on Virtual-1.
# Asserts: p1 lands on Virtual-1 on desktop 2, and the effect logs the re-desk for it.
# A FAIL means: p1 keeps desktop 1, which Virtual-1 does not show, so it is hidden.
set -u
NEST="$1"
LOG="$2"
. "$(dirname "$0")/../lib.sh"
kwjs_ok
need_outputs 2
newdesk 1 Two
per_output_desktops
js 'var o = null; for (var i = 0; i < workspace.screens.length; i++) { if (workspace.screens[i].name === "Virtual-1") { o = workspace.screens[i]; } } workspace.setCurrentDesktopForScreen(workspace.desktops[1], o);'
sleep 1
DESKS=$(jsread DESKS '(function () { var s = []; for (var i = 0; i < workspace.screens.length; i++) { s.push(workspace.screens[i].name + "=" + workspace.currentDesktopForScreen(workspace.screens[i]).x11DesktopNumber); } return s.join(" "); })()')
[ "$DESKS" = "Virtual-0=1 Virtual-1=2" ] || stage_fail "the outputs do not show desktops 1 and 2 ($DESKS)"
setting snapAssistEnabled false
assign Virtual-0 "$COLS3"
assign Virtual-1 "$COLS2"
sleep 2
open p1 300 300
ids
P1U=$(uuid p1)
[ -n "$P1U" ] || stage_fail "p1 never mapped"
P1="org.pz.plain|$P1U"
ZA=$(zoneid "$COLS3" 1)
ZB=$(zoneid "$COLS2" 1)
{ [ -n "$ZA" ] && [ -n "$ZB" ]; } || stage_fail "no zone 1 in COLS3 or Columns (2)"
PZ org.plasmazones.Snap.moveWindowToZone "$P1" "$ZA" >/dev/null
sleep 2
G=$(geo p1)
echo "p1 in COLS3 zone 1: $G"
{ [ "$(outof "$G")" = Virtual-0 ] && [ "$(deskof "$G")" = 1 ]; } || stage_fail "p1 is not on Virtual-0 desktop 1 ($G)"
emark
PZ org.plasmazones.Snap.moveWindowToZone "$P1" "$ZB" >/dev/null
sleep 2.5
G=$(geo p1)
echo "p1 after the move to Virtual-1: $G zone=$(zoneof "$P1")"
check "p1's output after the move" "$(outof "$G")" = Virtual-1
check "p1's desktops after the move" "$(deskof "$G")" = 2
check "re-desk lines for p1" "$(elog "Re-desking [^ ]*$P1U" | wc -l)" -ge 1
finish
