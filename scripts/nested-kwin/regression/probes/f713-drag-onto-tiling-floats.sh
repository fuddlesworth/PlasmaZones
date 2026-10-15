#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: GPL-3.0-or-later
#
# f713-drag-onto-tiling-floats: a plain drag of a window onto a tiling monitor leaves that
# monitor's tiles alone until the drop and floats the window there. Runner: 2 outputs, flag F.
# Staging: Snap Assist off. Virtual-0 COLS3, Virtual-1 autotile:bsp with t1 and t2. p1 is snapped
#   into zone 1 on Virtual-0, f1 sits free on Virtual-0. Each is Meta+left dragged with no snap
#   trigger onto the middle of Virtual-1, held about 5.5 s (sampled 2.6 s into the drag), then
#   dropped. After each drop Meta+F (toggle_window_float, the window active) tiles it.
# Asserts: for each window, t1 and t2 keep their frames mid-drag and after the drop; Virtual-1's
#   tiles do not change mid-drag; no "Window moved between monitors" line while the button is down;
#   after the drop the window floats on Virtual-1 with no zone and is not one of its tiles; Meta+F
#   makes it a tile there, not floating.
# A FAIL means: the crossing announced the window mid-drag and Virtual-1 reflowed under the pointer.
set -u
NEST="$1"
LOG="$2"
. "$(dirname "$0")/../lib.sh"
kwjs_ok
need_outputs 2
setting snapAssistEnabled false
assign Virtual-0 "$COLS3"
assign Virtual-1 "autotile:bsp"
sleep 2
for c in t1 t2 p1 f1; do
    open "$c" 300 300
    case "$c" in t*) out=Virtual-1 ;; *) out=Virtual-0 ;; esac
    sendto "$c" "$out" || stage_fail "$c not on $out ($(geo "$c"))"
done
read -r V1X V1Y V1W V1H <<< "$(scr Virtual-1)"
read -r V0X V0Y _ _ <<< "$(scr Virtual-0)"
moveto f1 $((V0X + 500)) $((V0Y + 300))
sleep 1
ids
P1=$(uuid p1)
F1=$(uuid f1)
[ -n "$P1" ] && [ -n "$F1" ] || stage_fail "p1 or f1 never mapped"
PZ org.plasmazones.Snap.moveWindowToZone "org.pz.plain|$P1" "$(zoneid "$COLS3" 1)" >/dev/null
sleep 2
Z=$(zoneof "org.pz.plain|$P1")
echo "p1 snapped: $(geo p1) zone='$Z' | f1 free: $(geo f1)"
case "$Z" in "{"*) ;; *) stage_fail "p1 did not snap ($Z)" ;; esac
PAIR0=$(frames t1 t2)
echo "t1/t2 tiled on Virtual-1: $PAIR0 | tiles $(tilelist Virtual-1)"
case "$PAIR0" in *"t1 "*"t2 "*) ;; *) stage_fail "t1 and t2 are not both on Virtual-1" ;; esac
DX=$((V1X + V1W / 2))
DY=$((V1Y + V1H / 2))
dragcheck() {
    local c="$1" u="$2" id="org.pz.plain|$2"
    local GX GY GW GH tiles0 mid after
    read -r GX GY GW GH _ <<< "$(geo "$c")"
    GX=$((${GX%.*} + ${GW%.*} / 2))
    GY=$((${GY%.*} + ${GH%.*} / 2))
    tiles0=$(tilelist Virtual-1)
    emark
    echo "== $c: Meta+left drag from $GX,$GY to $DX,$DY (Virtual-1's middle), no trigger, held about 5.5 s"
    fake abs "$GX" "$GY" wait 100 keydown 125 wait 50 down left wait 150 \
        path "$GX" "$GY" "$DX" "$DY" 20 30 wait 5500 up left wait 80 keyup 125 &
    local fipid=$!
    sleep 2.6
    mid=$(frames t1 t2)
    echo "  mid-drag: $c $(geo "$c") | $mid"
    check "$c mid-drag: t1 and t2 keep their frames" "$mid" = "$PAIR0"
    check "$c mid-drag: Virtual-1's tiles unchanged" "$(tilelist Virtual-1)" = "$tiles0"
    check "$c: transfer lines while the button is down" "$(elog "Window moved between monitors: .*$u" | wc -l)" -eq 0
    wait "$fipid"
    sleep 2.5
    after=$(frames t1 t2)
    echo "  after the drop: $c $(geo "$c") | $after"
    check "$c after the drop: t1 and t2 keep their frames" "$after" = "$PAIR0"
    check "$c after the drop: floating" "$(floating "$id")" = true
    check "$c after the drop: no zone" "$(zoneof "$id")" = ""
    check "$c after the drop: on Virtual-1" "$(outof "$(geo "$c")")" = Virtual-1
    check "$c after the drop: not one of Virtual-1's tiles" "$(tilelist Virtual-1)" '!~' "${u: -8}"
    activate "$c"
    shortcut toggle_window_float
    sleep 2.5
    echo "  after Meta+F: $c $(geo "$c") | $(frames t1 t2)"
    check "$c after Meta+F: not floating" "$(floating "$id")" = false
    check "$c after Meta+F: one of Virtual-1's tiles" "$(tilelist Virtual-1)" '~' "${u: -8}"
    PAIR0=$(frames t1 t2)
}
dragcheck p1 "$P1"
dragcheck f1 "$F1"
finish
