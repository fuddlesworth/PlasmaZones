#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: GPL-3.0-or-later
#
# f1151-scroll-visit-back-to-zone: a window on every desktop, snapped on a snapping desktop, goes
# back into its zone after a visit to a scrolling desktop on the same monitor (#1151). Runner:
# 1 output, no flags.
# Staging: two desktops on Virtual-0, desktop 1 running the four-column layout Q4 and desktop 2
#   scrolling. pw is set on all desktops and snapped into zone 2 on desktop 1, with a single-desktop
#   bystander by in zone 1. Desktop 2 is shown (the strip takes pw), then desktop 1 again.
# Asserts: back on desktop 1 pw's frame is its zone-2 rect again, the daemon logs the membership
#   pass re-applying it, and by has not moved.
# A FAIL means: only a window held in two snap stores was re-applied, so pw kept the strip rect it
#   was given on desktop 2.
set -u
NEST="$1"
LOG="$2"
. "$(dirname "$0")/../lib.sh"
kwjs_ok
newdesk 1 Two
setting snapAssistEnabled false
Q4=$(PZ org.plasmazones.LayoutRegistry.createLayoutFromJson '{"id":"{2b7e0c11-9a4d-4c1e-8f3b-6d5a4c3b2a19}","name":"Q4","type":1,"zones":[{"name":"A","zoneNumber":1,"relativeGeometry":{"x":0,"y":0,"width":0.25,"height":1.0}},{"name":"B","zoneNumber":2,"relativeGeometry":{"x":0.25,"y":0,"width":0.25,"height":1.0}},{"name":"C","zoneNumber":3,"relativeGeometry":{"x":0.5,"y":0,"width":0.25,"height":1.0}},{"name":"D","zoneNumber":4,"relativeGeometry":{"x":0.75,"y":0,"width":0.25,"height":1.0}}]}')
case "$Q4" in "{"*) ;; *) stage_fail "createLayoutFromJson returned no layout id ($Q4)" ;; esac
sleep 1
assignd Virtual-0 1 "$Q4"
assignd Virtual-0 2 "scrolling:"
sleep 2
desk 1
open pw 300 300
open by 300 300
ids
W=$(uuid pw)
B=$(uuid by)
[ -n "$W" ] && [ -n "$B" ] || stage_fail "pw or by never mapped"
on pw 'w[i].onAllDesktops = true;'
sleep 1.5
PZ org.plasmazones.Control.snapWindowToZone "org.pz.plain|$W" 2 Virtual-0 >/dev/null
PZ org.plasmazones.Control.snapWindowToZone "org.pz.plain|$B" 1 Virtual-0 >/dev/null
sleep 2
ZONE=$(rect "$(geo pw)")
BY=$(rect "$(geo by)")
echo "desktop 1: pw $(geo pw) by $(geo by)"
case "$(zoneof "org.pz.plain|$W")" in "{"*) ;; *) stage_fail "pw did not snap into zone 2" ;; esac

echo "== show desktop 2 (scrolling), then desktop 1"
desk 2
sleep 1
STRIP=$(rect "$(geo pw)")
echo "desktop 2: pw $(geo pw); strip $(striporder Virtual-0)"
[ "$STRIP" != "$ZONE" ] || stage_fail "the scrolling desktop left pw at its zone rect, so the visit tells nothing"
dmark
desk 1
sleep 1.5
echo "desktop 1 again: pw $(geo pw) by $(geo by)"
check "pw is back at its zone-2 rect" "$(rect "$(geo pw)")" = "$ZONE"
check "re-apply lines for desktop 1" "$(dlog "reconcileDesktopMemberships: re-applying" | wc -l)" -ge 1
check "by has not moved" "$(rect "$(geo by)")" = "$BY"
finish
