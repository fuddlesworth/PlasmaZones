#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: GPL-3.0-or-later
#
# f1022-new-desktop-move: a desktop KWin creates and moves a window onto in one call is in range
# at once. Runner: 1 output, no flags.
# Staging: two desktops, Virtual-0 COLS3 on every desktop, Snap Assist off, p1 snapped into zone 1
#   on desktop 1. Each try runs ONE KWin script that creates a desktop at the end and moves p1 onto
#   it in the same function, the shape of the window menu's Move to New Desktop. Three tries.
# Asserts: no try logs "virtualDesktop N out of range" or "desktop N past the last", and
#   WindowTracking.getWindowStatesForDesktop lists p1 on the new desktop after each move. Control:
#   a desktop inserted at the front renumbers p1's desktop up by one and the listing follows it.
set -u
NEST="$1"
LOG="$2"
. "$(dirname "$0")/../lib.sh"
kwjs_ok
setting snapAssistEnabled false
newdesk 1 D2
assign Virtual-0 "$COLS3"
sleep 1.5
echo "desktops: $(curdesk) | daemon count $(PZ org.plasmazones.LayoutRegistry.getVirtualDesktopCount)"
open p1
ids
P1U=$(uuid p1)
[ -n "$P1U" ] || stage_fail "p1 never mapped"
P1="org.pz.plain|$P1U"
PZ org.plasmazones.Control.snapWindowToZone "$P1" 1 Virtual-0 >/dev/null
sleep 2
Z=$(zoneof "$P1")
echo "p1 staged: $(geo p1) zone=$Z"
case "$Z" in "{"*) ;; *) stage_fail "p1 did not snap ($Z)" ;; esac
listed() {
    gdbus call --session --dest org.plasmazones --object-path /PlasmaZones \
        --method org.plasmazones.WindowTracking.getWindowStatesForDesktop Virtual-0 "$1" 2>&1 | grep -c "$P1U"
}
[ "$(listed 1)" = 1 ] || stage_fail "p1 is not listed on desktop 1 before the first try"
COUNTED=0
for try in 1 2 3; do
    N=$(($(PZ org.plasmazones.LayoutRegistry.getVirtualDesktopCount) + 1))
    echo "== try $try: create desktop $N and move p1 onto it in one script"
    dmark
    js "workspace.createDesktop(workspace.desktops.length, \"W8N$try\"); const nd = workspace.desktops[workspace.desktops.length - 1]; const w = workspace.windowList(); for (var i = 0; i < w.length; i++) { if (w[i].caption === \"p1\") { w[i].desktops = [nd]; print(\"F1022MOVED p1 to \" + nd.x11DesktopNumber + \" of \" + workspace.desktops.length); } }"
    sleep 2.5
    MV=$(grep "js: F1022MOVED" "$LOG" | tail -1 | sed 's/.*js: F1022MOVED //')
    case "$MV" in
    "p1 to $N "*) ;;
    *)
        note "the script did not report the move to $N (${MV:-nothing}); this try does not count"
        continue
        ;;
    esac
    COUNTED=$((COUNTED + 1))
    echo "  KWin: $MV | p1 $(geo p1) | daemon count $(PZ org.plasmazones.LayoutRegistry.getVirtualDesktopCount)"
    check "try $try: refusal lines naming desktop $N" \
        "$(dlog "setWindowMetadata: virtualDesktop $N out of range|screenDesktopChanged: desktop $N past the last" | wc -l)" -eq 0
    check "try $try: p1 listed on desktop $N" "$(listed "$N")" = 1
done
[ "$COUNTED" -gt 0 ] || stage_fail "no try's script reported its move"
echo "== control: a desktop inserted at the front"
K=$(deskof "$(geo p1)")
case "$K" in '' | *,*) stage_fail "p1 is not on exactly one desktop before the front insert ($(geo p1))" ;; esac
dmark
js 'workspace.createDesktop(0, "W8FRONT");'
sleep 3
K2=$(deskof "$(geo p1)")
check "p1's desktop in KWin after the front insert" "$K2" = "$((K + 1))"
check "p1 listed on desktop $K2" "$(listed "$K2")" = 1
check "refusal lines after the front insert" "$(dlog "out of range for instance|past the last" | wc -l)" -eq 0
finish
