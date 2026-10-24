#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: GPL-3.0-or-later
#
# fscale-fixed-device-pixels: a layout whose fixed zones count device pixels keeps them that size on
# a scaled output (#1160). Runner: 1 output, flag S.
# Staging: Virtual-0 is set to scale 1.25 with kscreen-doctor once the daemon runs (1600x900 device
#   pixels, 1280x720 logical), and the probe waits until KWin and the daemon both read the new
#   scale. The layouts are created and assigned after that, so the re-resolve on a scale change is
#   not what this exercises. Two layouts with the same three fixed zones, 400 + 800 + 400 by 900,
#   full screen and no gaps: DEV with fixedGeometryInDevicePixels, LOG without (the default). Each
#   is assigned to Virtual-0 in turn and the window w is snapped into its zone 2 over D-Bus.
# Asserts: under DEV, w's frame is the zone divided by the scale, 320,0 640x720 (within 2 px). LOG is
#   the control: its numbers are logical pixels, so zone 2 sits at 400,0 and is 800 wide, as before.
# A FAIL means: device-pixel fixed geometry was used as logical pixels, so the zones grew with the
#   display scaling and overflowed the screen.
set -u
NEST="$1"
LOG="$2"
. "$(dirname "$0")/../lib.sh"
kwjs_ok
on_exit 'ksd output.Virtual-0.scale.1 >/dev/null'
ksd output.Virtual-0.scale.1.25
# The logical size and the daemon's scale follow the change a moment apart; wait for both.
OW=""
OH=""
SCALE=""
for _ in $(seq 10); do
    sleep 1
    SCR=$(screens)
    read -r OX OY OW OH <<< "$(scr Virtual-0)"
    SCALE=$(PZ org.plasmazones.Screen.getScreenInfo Virtual-0 | python3 -c 'import json,sys; print(json.load(sys.stdin).get("devicePixelRatio"))' 2>/dev/null)
    [ "$OW" = 1280 ] && [ "$OH" = 720 ] && [ "$SCALE" = 1.25 ] && break
done
echo "Virtual-0 ${OX:-?},${OY:-?} ${OW}x$OH, scale $SCALE"
[ "$OW" = 1280 ] && [ "$OH" = 720 ] || stage_fail "Virtual-0 is not 1280x720 logical at scale 1.25 (${OW}x$OH)"
[ "$SCALE" = 1.25 ] || stage_fail "the daemon reads scale $SCALE for Virtual-0, not 1.25"
zones='"zones":[
 {"name":"A","zoneNumber":1,"geometryMode":1,"fixedGeometry":{"x":0,"y":0,"width":400,"height":900},"relativeGeometry":{"x":0,"y":0,"width":0.25,"height":1}},
 {"name":"B","zoneNumber":2,"geometryMode":1,"fixedGeometry":{"x":400,"y":0,"width":800,"height":900},"relativeGeometry":{"x":0.25,"y":0,"width":0.5,"height":1}},
 {"name":"C","zoneNumber":3,"geometryMode":1,"fixedGeometry":{"x":1200,"y":0,"width":400,"height":900},"relativeGeometry":{"x":0.75,"y":0,"width":0.25,"height":1}}]'
common='"type":1,"useFullScreenGeometry":true,"zonePadding":0,"outerGap":0'
DEV=$(PZ org.plasmazones.LayoutRegistry.createLayoutFromJson \
    "{\"id\":\"{6d1f0a52-3c47-4f4e-9a8b-2e1d0c9b8a71}\",\"name\":\"DEV\",$common,\"fixedGeometryInDevicePixels\":true,$zones}")
LOGL=$(PZ org.plasmazones.LayoutRegistry.createLayoutFromJson \
    "{\"id\":\"{7e2a1b63-4d58-4a5f-8b9c-3f2e1d0c9b82}\",\"name\":\"LOG\",$common,$zones}")
case "$DEV$LOGL" in "{"*"}{"*"}") ;; *) stage_fail "createLayoutFromJson returned no layout ids ($DEV / $LOGL)" ;; esac
open w 300 300
ids
W=$(uuid w)
[ -n "$W" ] || stage_fail "w never mapped"

# snapped LAYOUT: assign LAYOUT, snap w into its zone 2 and leave w's rect in SX SY SW SH. Call it
# directly, never in $( ), so its stage_fail ends the probe.
snapped() {
    local z2
    assign Virtual-0 "$1"
    sleep 2
    z2=$(zoneid "$1" 2)
    [ -n "$z2" ] || stage_fail "zone 2 of $1 has no id"
    PZ org.plasmazones.Control.snapWindowToZone "org.pz.plain|$W" 2 Virtual-0 >/dev/null
    sleep 2
    [ "$(zoneof "org.pz.plain|$W")" = "$z2" ] || stage_fail "w did not snap into zone 2 of $1 ($(zoneof "org.pz.plain|$W"))"
    read -r SX SY SW SH <<< "$(rect "$(geo w)")"
    SX=${SX%.*}
    SY=${SY%.*}
    SW=${SW%.*}
    SH=${SH%.*}
}

snapped "$DEV"
read -r DX DY DW DH <<< "$SX $SY $SW $SH"
echo "DEV: w $DX,$DY ${DW}x$DH"
snapped "$LOGL"
read -r LX LY LW LH <<< "$SX $SY $SW $SH"
echo "LOG: w $LX,$LY ${LW}x$LH"

check "DEV zone 2 left edge at 400/1.25" "$(within "$DX" 320)" = yes
check "DEV zone 2 top edge at 0" "$(within "$DY" 0)" = yes
check "DEV zone 2 width 800/1.25" "$(within "$DW" 640)" = yes
check "DEV zone 2 height 900/1.25" "$(within "$DH" 720)" = yes
check "LOG zone 2 left edge at 400 (control)" "$(within "$LX" 400)" = yes
check "LOG zone 2 width 800 (control)" "$(within "$LW" 800)" = yes
finish
