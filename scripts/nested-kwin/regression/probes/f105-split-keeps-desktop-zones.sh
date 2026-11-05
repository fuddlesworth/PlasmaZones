#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: GPL-3.0-or-later
#
# f105-split-keeps-desktop-zones: splitting a monitor into virtual screens keeps the zones of a
# window that sits on two desktops. Runner: 1 output, no flags.
# Staging: two desktops, snapping on both with a four-column layout Q4. pw is on desktops 1 and 2,
#   snapped into zone 1 on desktop 1 and zone 2 on desktop 2. Virtual-0 is split 50/50 into
#   virtual screens, move_window_right is pressed on pw on desktop 1, desktop 2 is shown, and the
#   split is removed again.
# Asserts: the split logs "rekeyMembershipsToRecordedScreens: N membership(s) re-filed" beside
#   the virtual-screen migration; pw keeps its zone on desktop 2 through the split and on both
#   desktops after the un-split; no release line names pw.
# A FAIL means: the split's own virtual-screen resnap dropped the desktop-2 membership.
set -u
NEST="$1"
LOG="$2"
. "$(dirname "$0")/../lib.sh"
kwjs_ok
newdesk 1 Two
setting snapAssistEnabled false
Q4=$(PZ org.plasmazones.LayoutRegistry.createLayoutFromJson '{"id":"{7a1d0c55-4f2e-4b8a-9d3c-1e5f6a7b8c91}","name":"Q4","type":1,"zones":[{"name":"A","zoneNumber":1,"relativeGeometry":{"x":0,"y":0,"width":0.25,"height":1.0}},{"name":"B","zoneNumber":2,"relativeGeometry":{"x":0.25,"y":0,"width":0.25,"height":1.0}},{"name":"C","zoneNumber":3,"relativeGeometry":{"x":0.5,"y":0,"width":0.25,"height":1.0}},{"name":"D","zoneNumber":4,"relativeGeometry":{"x":0.75,"y":0,"width":0.25,"height":1.0}}]}')
echo "Q4 layout: $Q4"
case "$Q4" in "{"*) ;; *) stage_fail "createLayoutFromJson returned no layout id ($Q4)" ;; esac
sleep 1
assign Virtual-0 "$Q4"
sleep 2
open pw 300 300
ids
W=$(uuid pw)
[ -n "$W" ] || stage_fail "pw never mapped"
ID="org.pz.plain|$W"
on pw 'w[i].desktops = [workspace.desktops[0], workspace.desktops[1]]; workspace.activeWindow = w[i];'
sleep 1.5
pwstate() { echo "cur=$(curdesk) pw $(geo pw) zone='$(zoneof "$ID")'"; }
desk 1
PZ org.plasmazones.Control.snapWindowToZone "$ID" 1 Virtual-0 >/dev/null
sleep 1.5
desk 2
PZ org.plasmazones.Control.snapWindowToZone "$ID" 2 Virtual-0 >/dev/null
sleep 1.5
Z2=$(zoneof "$ID")
echo "desktop 2 before the split: $(pwstate)"
desk 1
Z1=$(zoneof "$ID")
echo "desktop 1 before the split: $(pwstate)"
case "$Z1" in "{"*) ;; *) stage_fail "pw did not snap into zone 1 on desktop 1 ($Z1)" ;; esac
case "$Z2" in "{"*) ;; *) stage_fail "pw did not snap into zone 2 on desktop 2 ($Z2)" ;; esac
[ "$Z1" != "$Z2" ] || stage_fail "pw reads the same zone on both desktops before the split"
dmark
SPLIT='{"screens":[{"index":0,"displayName":"L","region":{"x":0,"y":0,"width":0.5,"height":1}},{"index":1,"displayName":"R","region":{"x":0.5,"y":0,"width":0.5,"height":1}}]}'
PZ org.plasmazones.Screen.setVirtualScreenConfig Virtual-0 "$SPLIT" >/dev/null
sleep 3
for vs in Virtual-0/vs:0 Virtual-0/vs:1; do
    L=$(PZ org.plasmazones.LayoutRegistry.getLayoutForScreen "$vs")
    [ "$L" = "$Q4" ] || stage_fail "$vs does not run Q4 after the split ($L)"
done
echo "desktop 1 after the split: $(pwstate)"
check "virtual-screen migration lines at the split" "$(dlog "Migrated [0-9]+ window screen assignments" | wc -l)" -ge 1
check "membership re-file lines at the split" "$(dlog "rekeyMembershipsToRecordedScreens: [0-9]+ membership\(s\) re-filed" | wc -l)" -ge 1
check "release lines naming pw at the split" "$(dlog "released .*$W" | wc -l)" -eq 0
dmark
activate pw
shortcut move_window_right
sleep 2
echo "desktop 1 after move_window_right: $(pwstate)"
desk 2
echo "desktop 2 after the split: $(pwstate)"
check "pw's zone on desktop 2 after the split" "$(zoneof "$ID")" = "$Z2"
check "release lines naming pw since move_window_right" "$(dlog "released .*$W|releaseMembership.*$W" | wc -l)" -eq 0
dmark
PZ org.plasmazones.Screen.setVirtualScreenConfig Virtual-0 '{"screens":[]}' >/dev/null
sleep 3
echo "desktop 2 after the un-split: $(pwstate)"
check "pw's zone on desktop 2 after the un-split" "$(zoneof "$ID")" = "$Z2"
desk 1
echo "desktop 1 after the un-split: $(pwstate)"
check "pw's zone on desktop 1 after the un-split" "$(zoneof "$ID")" = "$Z1"
check "release lines naming pw at the un-split" "$(dlog "released .*$W" | wc -l)" -eq 0
finish
