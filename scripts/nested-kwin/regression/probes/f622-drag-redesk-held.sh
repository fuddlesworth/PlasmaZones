#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: GPL-3.0-or-later
#
# f622-drag-redesk-held: KWin's re-desk of a window dragged onto another output under per-output
# desktops waits for the drop. Runner: 2 outputs, flags F.
# Staging: two desktops, per-output desktops on, Virtual-0 shows desktop 1 and Virtual-1 desktop 2,
#   both COLS3, Snap Assist off. p1 is snapped into zone 1 on Virtual-0, then Meta-dragged onto
#   Virtual-1 and dropped free (no activation trigger).
# Asserts: p1 ends on Virtual-1 on desktop 2 with no zone, and the daemon logs no desktop carry
#   during the drag.
# A FAIL means: the re-desk reached the daemon mid-drag and it carried p1 into a zone of a desktop
#   Virtual-0 does not show.
set -u
NEST="$1"
LOG="$2"
. "$(dirname "$0")/../lib.sh"
kwjs_ok
need_outputs 2
newdesk 1 Two
per_output_desktops
OUT1='var o = null; for (var i = 0; i < workspace.screens.length; i++) { if (workspace.screens[i].name === "Virtual-1") { o = workspace.screens[i]; } }'
js "$OUT1 workspace.setCurrentDesktopForScreen(workspace.desktops[1], o);"
sleep 1
DESKS=$(jsread DESKS '(function () { var s = []; for (var i = 0; i < workspace.screens.length; i++) { s.push(workspace.screens[i].name + "=" + workspace.currentDesktopForScreen(workspace.screens[i]).x11DesktopNumber); } return s.join(" "); })()')
echo "per-output desktops: $DESKS"
[ "$DESKS" = "Virtual-0=1 Virtual-1=2" ] || stage_fail "the outputs do not show desktops 1 and 2 ($DESKS)"
setting snapAssistEnabled false
for s in Virtual-0 Virtual-1; do
    assign "$s" "$COLS3"
done
sleep 2
open p1 300 300
ids
P1U=$(uuid p1)
[ -n "$P1U" ] || stage_fail "p1 never mapped"
P1="org.pz.plain|$P1U"
Z1=$(zoneid "$COLS3" 1)
[ -n "$Z1" ] || stage_fail "no zone 1 in COLS3"
PZ org.plasmazones.Snap.moveWindowToZone "$P1" "$Z1" >/dev/null
sleep 2
G=$(geo p1)
echo "p1 snapped: $G zone=$(zoneof "$P1")"
case "$(zoneof "$P1")" in "{"*) ;; *) stage_fail "p1 did not snap into COLS3 zone 1" ;; esac
[ "$(outof "$G")" = Virtual-0 ] || stage_fail "p1 is not on Virtual-0 ($G)"
dmark
# Grab inside zone 1 (8..337 x 8..760) and carry it onto Virtual-1.
fake abs 170 300 wait 100 keydown 125 wait 50 down left wait 150 \
    path 170 300 1400 300 30 30 wait 600 up left wait 80 keyup 125
sleep 2.5
G=$(geo p1)
echo "p1 after the drop: $G zone='$(zoneof "$P1")'"
check "p1's output after the drop" "$(outof "$G")" = Virtual-1
check "p1's desktops after the drop" "$(deskof "$G")" = 2
check "p1's zone after the free drop" "$(zoneof "$P1")" '!~' '[0-9a-f]{8}-'
check "desktop carry lines during the drag" "$(dlog "reconcileDesktopMemberships: carrying" | wc -l)" -eq 0
finish
