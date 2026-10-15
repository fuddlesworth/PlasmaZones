#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: GPL-3.0-or-later
#
# f780-hidden-float-back: a window kept floating on a hidden desktop gets its float-back when that
# desktop is first shown. Runner: 1 output, flag S.
# Staging: two desktops, both Grid (2x2) (800x450 zones), Snap Assist off. w and v (1000x700, so the
#   float-back is a grow over a zone) are placed free, w at 300,100 and v at 500,150, and put on
#   both desktops. On desktop 2 each is snapped (w zone 1, v zone 2) and floated with the float
#   shortcut; on desktop 1 each is snapped (w zone 1, v zone 4). Then, on desktop 1, a KWin script
#   puts both on desktop 2 only while it is hidden, and v's frame is moved by +150,+80 (the user's
#   move while hidden). Desktop 2 is shown, then 1, then 2 again.
# Asserts: on the first showing one "re-stating the float-back of <w>" line and one
#   applyGeometryRequested for w at its free spot, and w's frame is that spot; none for v, which
#   stays where it was moved; the second showing adds no line and no apply.
# A FAIL means: w floated on desktop 2 at desktop 1's zone 1 rect instead of its own float spot.
set -u
NEST="$1"
LOG="$2"
. "$(dirname "$0")/../lib.sh"
kwjs_ok
newdesk 1 D2
setting snapAssistEnabled false
assign Virtual-0 "$GRID4"
sleep 2
desk 1
for c in w v; do
    open "$c" 1000 700
done
place w 300 100
place v 500 150
ids
WU=$(uuid w)
VU=$(uuid v)
[ -n "$WU" ] && [ -n "$VU" ] || stage_fail "w or v never mapped"
W="org.pz.plain|$WU"
V="org.pz.plain|$VU"
# spotstate w|v -> "<spot> desks=D cur=C floating=B zone='ZONE'"
spotstate() {
    local id="$W"
    [ "$1" = v ] && id="$V"
    echo "$(wprop "$1" 'w.frameGeometry.x + "," + w.frameGeometry.y + " " + w.frameGeometry.width + "x" + w.frameGeometry.height + " desks=" + w.desktops.map(function (d) { return d.x11DesktopNumber; }) + " cur=" + workspace.currentDesktop.x11DesktopNumber') floating=$(floating "$id") zone='$(zoneof "$id")'"
}
# spot CAPTION -> "x,y wxh" (lib's frame adds the output and maximize state, which the spot
# comparisons and the x/y/size split below must not carry)
spot() { wprop "$1" 'w.frameGeometry.x + "," + w.frameGeometry.y + " " + w.frameGeometry.width + "x" + w.frameGeometry.height'; }
WSPOT=$(spot w)
VSPOT=$(spot v)
echo "free spots: w $WSPOT | v $VSPOT"
on w 'w[i].desktops = [workspace.desktops[0], workspace.desktops[1]];'
on v 'w[i].desktops = [workspace.desktops[0], workspace.desktops[1]];'
sleep 1.5
desk 2
PZ org.plasmazones.Control.snapWindowToZone "$W" 1 Virtual-0 >/dev/null
sleep 1.5
PZ org.plasmazones.Control.snapWindowToZone "$V" 2 Virtual-0 >/dev/null
sleep 1.5
echo "desktop 2, snapped: w $(spotstate w) | v $(spotstate v)"
floatkey w
floatkey v
SW=$(spotstate w)
echo "desktop 2, floated: w $SW | v $(spotstate v)"
case "$SW" in *"desks=1,2 cur=2 floating=true"*) ;; *) stage_fail "w not floating on desktop 2 while on both desktops" ;; esac
desk 1
PZ org.plasmazones.Control.snapWindowToZone "$W" 1 Virtual-0 >/dev/null
sleep 1.5
PZ org.plasmazones.Control.snapWindowToZone "$V" 4 Virtual-0 >/dev/null
sleep 1.5
SW=$(spotstate w)
echo "desktop 1, snapped: w $SW | v $(spotstate v)"
case "$SW" in *"floating=false"*) ;; *) stage_fail "w not snapped on desktop 1" ;; esac
MON="$NEST/f780-monitor.log"
mon_start "$MON" "type='signal',interface='org.plasmazones.WindowTracking',member='applyGeometryRequested'"
mmark "$MON"
on w 'w[i].desktops = [workspace.desktops[1]];'
on v 'w[i].desktops = [workspace.desktops[1]];'
sleep 2
on v 'var g = w[i].frameGeometry; w[i].frameGeometry = {x: g.x + 150, y: g.y + 80, width: g.width, height: g.height};'
sleep 1.5
VMOVED=$(spot v)
echo "on desktop 2 only while it is hidden (v then moved by +150,+80): w $(spotstate w) | v $(spotstate v)"
# applies ID: the applyGeometryRequested signals naming ID since MMARK, "id x y w h ..." each
applies() { msgs "$MON" | grep "^applyGeometryRequested " | grep -F "$1"; }
dmark
desk 2
sleep 1
echo "first showing of desktop 2: w $(spotstate w) | v $(spotstate v)"
dlog "re-stating the float-back of.*" | sed 's/^/    /'
applies "$WU" | sed 's/^/    /'
read -r WX WY <<< "$(echo "$WSPOT" | sed 's/ .*//; s/,/ /')"
WSIZE=$(echo "$WSPOT" | sed 's/.* //')
check "first showing: re-statement lines for w" "$(dlog "re-stating the float-back of [^ ]*$WU" | wc -l)" -eq 1
check "first showing: re-statement lines for v" "$(dlog "re-stating the float-back of [^ ]*$VU" | wc -l)" -eq 0
check "first showing: applies for w at its free spot" \
    "$(applies "$WU" | awk -v x="$WX" -v y="$WY" -v s="$WSIZE" '$3 == x && $4 == y && $5 "x" $6 == s { n++ } END { print n + 0 }')" -eq 1
check "first showing: applies for v" "$(applies "$VU" | wc -l)" -eq 0
check "first showing: w back at its free spot" "$(spot w)" = "$WSPOT"
check "first showing: v where it was moved" "$(spot v)" = "$VMOVED"
NAPPLY=$(applies "$WU" | wc -l)
dmark
desk 1
desk 2
sleep 1
echo "second showing of desktop 2: w $(spotstate w) | v $(spotstate v)"
check "second showing: re-statement lines" "$(dlog "re-stating the float-back of" | wc -l)" -eq 0
check "second showing: new applies for w" "$(($(applies "$WU" | wc -l) - NAPPLY))" -eq 0
check "second showing: w at its free spot" "$(spot w)" = "$WSPOT"
finish
