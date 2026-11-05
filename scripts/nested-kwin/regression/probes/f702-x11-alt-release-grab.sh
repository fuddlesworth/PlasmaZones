#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: GPL-3.0-or-later
#
# f702-x11-alt-release-grab: an X11 window whose free size comes back mid-drag (the snap trigger let
# go) stays under the pointer. Runner: 1 output, flags XF.
# Staging: Virtual-0 with a one-zone layout over the left half, Snap Assist off. x1 (an X11
#   plain_gtk, 150x150) is snapped into the zone with an Alt drag, then grabbed near the zone's
#   right edge and Meta-dragged with Alt held, Alt let go without a release, which restores its free
#   size during the drag, then one more pointer step and the release.
# Asserts: with the pointer still after Alt is let go, and again after one pointer step, the
#   pointer's x lies inside x1's frame.
# A FAIL means: x1 kept the zone's top-left at its free size, so the pointer lay right of it.
set -u
NEST="$1"
LOG="$2"
. "$(dirname "$0")/../lib.sh"
kwjs_ok
setting snapAssistEnabled false
HALF=$(PZ org.plasmazones.LayoutRegistry.createLayoutFromJson '{"id":"{5e1f0c2a-7d3b-4a8e-9c61-2b4d8f0a1e77}","name":"Half","type":1,"zones":[{"name":"Left","zoneNumber":1,"relativeGeometry":{"x":0,"y":0,"width":0.5,"height":1.0}}]}')
case "$HALF" in "{"*) ;; *) stage_fail "createLayoutFromJson refused the half layout ($HALF)" ;; esac
sleep 1
assign Virtual-0 "$HALF"
sleep 2
read -r _ _ SW _ <<< "$(scr Virtual-0)"
xclient plain_gtk.py x1 150 150
moveto x1 600 300
sleep 1
FREE=$(geo x1)
echo "x1 free: $FREE x11=$(wprop x1 '!w.wayland')"
[ -n "$FREE" ] || stage_fail "x1 never mapped"
read -r GX GY <<< "$(centre x1)"
fake abs "$GX" "$GY" wait 100 keydown 125 wait 50 down left wait 150 \
    keydown 56 wait 150 path "$GX" "$GY" 250 384 20 30 wait 900 up left wait 80 keyup 56 keyup 125
sleep 2
G=$(geo x1)
echo "x1 snapped: $G"
read -r ZX ZY ZW ZH _ <<< "$G"
ZX=${ZX%.*} ZY=${ZY%.*} ZW=${ZW%.*} ZH=${ZH%.*}
[ "$ZX" -lt 50 ] && [ "$ZW" -gt $((SW / 3)) ] && [ "$ZW" -le $((SW / 2)) ] \
    || stage_fail "x1 did not snap into the left-half zone ($G)"
PX=$((ZX + ZW - 25))
PY=$((ZY + ZH / 2))
QX=$((PX + 30))
# Alt held at the start activates the drag, and letting go of it clears the zone geometry: the
# daemon restores the free size during the drag. Alt's release is itself a tick, so the restore can
# land with the pointer still. Read once while it holds still, then after one pointer step: KWin
# re-anchors an X11 move on the next motion.
fake abs "$PX" "$PY" wait 100 keydown 125 wait 50 down left wait 150 \
    keydown 56 wait 150 path "$PX" "$PY" "$QX" "$PY" 6 40 wait 300 keyup 56 wait 2200 \
    abs $((QX + 1)) "$PY" wait 2000 up left wait 80 keyup 125 &
FIPID=$!
on_exit "kill $FIPID 2>/dev/null"
inside() {
    local x w
    read -r x _ w _ <<< "$1"
    x=${x%.*}
    w=${w%.*}
    if [ -n "$x" ] && [ "$2" -ge "$x" ] && [ "$2" -le $((x + w)) ]; then echo inside; else echo "outside ($1)"; fi
}
sleep 2.4
G=$(geo x1)
echo "still, after Alt up: x1 $G pointer=$QX,$PY"
[ "$(size "$G")" = "$(size "$FREE")" ] || stage_fail "x1's free size did not come back during the drag ($G)"
check "pointer x $QX inside x1 with the pointer still" "$(inside "$G" "$QX")" = inside
sleep 1.8
G=$(geo x1)
echo "after one step: x1 $G pointer=$((QX + 1)),$PY"
check "pointer x $((QX + 1)) inside x1 after one step" "$(inside "$G" $((QX + 1)))" = inside
wait "$FIPID"
sleep 1
echo "after release: $(geo x1)"
finish
