#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: GPL-3.0-or-later
#
# f1058-cancelled-suspended-tile: a cancelled Reorder-mode drag puts back a tile a snapping screen
# suspended. Runner: 2 outputs, flag F.
# Staging: Virtual-0 autotile:bsp with t1 and t2, Virtual-1 COLS3, autotileDragBehavior 1
#   (Reorder), Snap Assist off. A probe rule "app org.pz.plain and isTiled -> hide title bar" shows
#   the tiled mark as noBorder. A dbus-monitor records Tiling.retile and
#   Scrolling.reapplyWindowGeometry calls. The gesture is a KEYBOARD move (a pointer drag holds the
#   effect's keyboard grab, which turns Escape into a cancelSnap): slotWindowMove, Right arrow
#   bursts until t1 is over Virtual-1 with noBorder false (the suspension), Left bursts until it is
#   over Virtual-0 again, then Escape (KWin cancels and puts t1 back at its start rect).
#   Control: a pointer Meta+left drag onto Virtual-1 and back, released on Virtual-0 (a drop).
# Asserts: after the Escape "endDrag cancelled: re-tiling" for t1, one retile naming Virtual-0, t1
#   back at its tile rect with noBorder true. Control: no cancelled-endDrag line and no retile.
# A FAIL means: the cancel left the tile with its tiled mark shed until an unrelated batch.
set -u
NEST="$1"
LOG="$2"
. "$(dirname "$0")/../lib.sh"
kwjs_ok
need_outputs 2
read -r BX BY BW BH <<< "$(scr Virtual-1)"
setting snapAssistEnabled false
assign Virtual-0 "autotile:bsp"
assign Virtual-1 "$COLS3"
setting autotileDragBehavior 1
sleep 2
RULE="{11111111-2222-4333-8444-55555f105801}"
R='{"actions":[{"value":true,"type":"setHideTitleBar"}],"enabled":true,"id":"'"$RULE"'","match":{"all":[{"field":"appId","op":"equals","value":"org.pz.plain"},{"field":"isTiled","op":"equals","value":true}]},"name":"probe F1058 tiled look","priority":50}'
[ "$(PZ org.plasmazones.Rules.addRule "$R")" = true ] || stage_fail "the tiled-look rule was not added"
on_exit 'PZ org.plasmazones.Rules.removeRule "$RULE" >/dev/null'
sleep 1
for c in t1 t2; do
    open "$c"
    sendto "$c" Virtual-0 || stage_fail "$c not on Virtual-0"
done
sleep 2
ids
T1U=$(uuid t1)
[ -n "$T1U" ] || stage_fail "t1 never mapped"
MON="$NEST/f1058-monitor.log"
mon_start "$MON" "type='method_call',member='retile'" "type='method_call',member='reapplyWindowGeometry'"
G1=$(geo t1)
echo "staged: t1 $G1 | t2 $(geo t2) | tiles $(tiles Virtual-0)"
case "$(tiles Virtual-0)" in 2*) ;; *) stage_fail "Virtual-0 does not hold two tiles" ;; esac
[ "$(nbof "$G1")" = true ] || stage_fail "the tiled-look rule did not show on t1 (noBorder not true while tiled)"
# keyburst KEY: BURST presses of KEY (106 Right, 105 Left)
BURST=12
keyburst() {
    local args=()
    for _ in $(seq "$BURST"); do args+=(keydown "$1" wait 20 keyup "$1" wait 20); done
    fake "${args[@]}" wait 150
}
escape() { fake keydown 1 wait 40 keyup 1; }
# kbexcursion: the keyboard move of t1 onto Virtual-1 (until over Virtual-1 with nb=false, the
# suspension) and back (until over Virtual-0), then Escape; at most MAXB bursts each way
MAXB=20
kbexcursion() {
    local g b
    activate t1
    mmark "$MON"
    emark
    js 'workspace.slotWindowMove();'
    sleep 0.5
    [ "$(wprop t1 'w.move')" = true ] || stage_fail "the keyboard move of t1 did not start"
    b=0
    while :; do
        keyburst 106
        b=$((b + 1))
        g=$(geo t1)
        case "$g" in *"nb=false"*"@Virtual-1") break ;; esac
        if [ "$b" -ge "$MAXB" ]; then
            escape
            sleep 2
            stage_fail "after $b Right bursts t1 is not over Virtual-1 with nb=false ($g): no suspension"
        fi
    done
    echo "  t1 over Virtual-1 after $b Right bursts: $g"
    sleep 1.5
    b=0
    while :; do
        keyburst 105
        b=$((b + 1))
        g=$(geo t1)
        case "$g" in *"@Virtual-0") break ;; esac
        if [ "$b" -ge "$MAXB" ]; then
            escape
            sleep 2
            stage_fail "after $b Left bursts t1 is not back over Virtual-0 ($g)"
        fi
    done
    echo "  t1 back over Virtual-0 after $b Left bursts, before Escape: $g"
    sleep 1.2
    escape
    sleep 2.5
}
# drop: the control, a pointer Meta+left drag onto Virtual-1 and back, released on Virtual-0
drop() {
    local x y w h cx cy
    read -r x y w h _ <<< "$(geo t1)"
    cx=$((${x%.*} + ${w%.*} / 2))
    cy=$((${y%.*} + ${h%.*} / 2))
    activate t1
    mmark "$MON"
    emark
    fake abs "$cx" "$cy" wait 100 keydown 125 wait 50 down left wait 150 \
        path "$cx" "$cy" $((BX + BW / 2)) $((BY + BH / 2)) 20 30 wait 2500 \
        path $((BX + BW / 2)) $((BY + BH / 2)) "$cx" "$cy" 20 30 wait 1200 up left wait 80 keyup 125
    sleep 2.5
}
echo "== keyboard move Virtual-0 -> Virtual-1 -> Virtual-0, Escape"
kbexcursion
A=$(geo t1)
echo "  t1 $A | t2 $(geo t2)"
elog "Window move finished without button release, reverted= [a-z]*" | head -1 | grep -q "reverted= true" \
    || stage_fail "KWin did not cancel the move (no reverted= true)"
elog "endDrag outcome: .*$T1U" | grep -q "action= 4" || stage_fail "the endDrag outcome for t1 is not action= 4 (CancelSnap)"
[ "$(rect "$A")" = "$(rect "$G1")" ] || stage_fail "KWin did not put t1 back at its tile rect ($A)"
check "'endDrag cancelled: re-tiling' lines for t1" "$(elog "endDrag cancelled: re-tiling [^ ]* for [^ ]*$T1U" | wc -l)" -eq 1
check "retile calls naming Virtual-0 since the gesture" "$(msgs "$MON" | grep -c "^retile Virtual-0")" -eq 1
check "t1's tiled look (noBorder) back at the read" "$(nbof "$A")" = true
echo "== control: a pointer drag onto Virtual-1 and back, dropped on Virtual-0"
PZ org.plasmazones.Tiling.retile Virtual-0 >/dev/null
sleep 2
drop
echo "  t1 $(geo t1) | t2 $(geo t2) | tiles $(tiles Virtual-0)"
check "control: cancelled-endDrag lines for t1 after a drop" "$(elog "endDrag cancelled: re-tiling [^ ]* for [^ ]*$T1U" | wc -l)" -eq 0
check "control: retile calls after a drop" "$(msgs "$MON" | grep -c "^retile ")" -eq 0
finish
