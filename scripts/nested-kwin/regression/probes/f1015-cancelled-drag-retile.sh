#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: GPL-3.0-or-later
#
# f1015-cancelled-drag-retile: an Escape-cancelled Float-mode engine drag has the daemon re-emit
# the tile (F1015, with F1014). Runner: 1 output, flags F, PZ_F1015_RUN=monocle, bsp or scroll.
# Staging: the default autotile drag behaviour (Float), so Escape reaches KWin's move filter. A
#   dbus-monitor records Tiling.retile and Scrolling.reapplyWindowGeometry calls. The gesture: a
#   Meta+left drag from the window's centre by 40 px, held 2.5 s, Escape with the button down,
#   release, read 2.5 s later.
#   monocle: Virtual-0 autotile:monocle, m2 then m1, both maximized at the gap-inset tile; Escape
#     on m1.
#   bsp: Virtual-0 autotile:bsp, t1 and t2, a rule "app org.pz.plain and isTiled -> hide title bar"
#     (noBorder true while tiled); Escape on t1. Control 1: t1 dragged 200 px and dropped (no
#     Escape). Control 2: t2 floated first (toggle_window_float), then the Escape gesture on it.
#   scroll: Virtual-0 scrolling, c1 and c2; Escape on the column wholly inside the viewport.
# Asserts: monocle: one retile of Virtual-0, no reapply, m1 back at its tile rect maximized, not
#   floating. bsp: one retile, t1 back at its tile rect with noBorder true; the drop and the drag of
#   an already floating window make no retile and no reapply. scroll: one reapply naming the tile,
#   no retile, the tile back at its column rect.
# A FAIL means: the cancel left the tile at KWin's restored frame with no re-emit, so its tiled
#   look and rect waited for an unrelated batch.
set -u
NEST="$1"
LOG="$2"
. "$(dirname "$0")/../lib.sh"
kwjs_ok
[ -x "$PZR_FAKEINPUT" ] || stage_fail "no fakeinput binary (the probe needs flag F; run.sh builds it)"
RUN="${PZ_F1015_RUN:-monocle}"
MON="$NEST/f1015-monitor.log"
mon_start "$MON" "type='method_call',interface='org.plasmazones.Tiling',member='retile'" \
    "type='method_call',interface='org.plasmazones.Scrolling',member='reapplyWindowGeometry'"
# ncalls MEMBER [ARG]: MEMBER calls since mmark, only those naming ARG when given
ncalls() { msgs "$MON" | awk -v m="$1" -v a="${2:-}" '$1 == m && (a == "" || index($0, a)) { n++ } END { print n + 0 }'; }
# gesture CAPTION KIND: KIND escape (hold, Escape with the button down, release) or drop (released
# 200 px away); sets the marks the reads use
gesture() {
    local g x y w h cx cy
    g=$(geo "$1")
    read -r x y w h _ <<< "$g"
    cx=$((${x%.*} + ${w%.*} / 2))
    cy=$((${y%.*} + ${h%.*} / 2))
    activate "$1"
    mmark "$MON"
    emark
    dmark
    # KEY_LEFTMETA = 125, KEY_ESC = 1.
    if [ "$2" = escape ]; then
        fake abs "$cx" "$cy" wait 100 keydown 125 wait 50 down left wait 150 \
            path "$cx" "$cy" $((cx + 40)) "$cy" 8 30 wait 2500 keydown 1 wait 40 keyup 1 wait 300 up left wait 50 keyup 125
    else
        fake abs "$cx" "$cy" wait 100 keydown 125 wait 50 down left wait 150 \
            path "$cx" "$cy" $((cx - 200)) $((cy - 80)) 10 30 wait 1500 up left wait 80 keyup 125
    fi
    sleep 2.5
    echo "  calls since the gesture: $(msgs "$MON" | tr '\n' ';')"
    echo "  endDrag outcome: $(elog "endDrag outcome: .*" | grep -o "action=.*" | head -1 | cut -c1-80)"
}
# cancelled UUID: the gesture was a cancel KWin reverted, ending in endDrag CancelSnap
cancelled() {
    elog "Window move finished without button release, reverted= true" | grep -q . \
        || stage_fail "KWin did not revert the cancelled move of ...${1: -8}"
    [ "$(elog "endDrag outcome: [^ ]*$1.*" | grep -o "action= [0-9]*" | head -1)" = "action= 4" ] \
        || stage_fail "the cancelled drag of ...${1: -8} did not end as CancelSnap"
}

case "$RUN" in
monocle)
    assign Virtual-0 "autotile:monocle"
    sleep 2
    open m2
    open m1
    ids
    M1=$(uuid m1)
    [ -n "$M1" ] || stage_fail "m1 never mapped"
    activate m1
    TILE=$(geo m1)
    echo "staged: m1 $TILE | m2 $(geo m2)"
    [ "$(maxof "$TILE")" = 3 ] && [ "$(maxof "$(geo m2)")" = 3 ] || stage_fail "no monocle (both maximized)"
    echo "== Escape on the dragged Monocle tile m1"
    gesture m1 escape
    cancelled "$M1"
    A1=$(geo m1)
    echo "  m1 $A1 | m2 $(geo m2)"
    check "retile calls naming Virtual-0" "$(ncalls retile Virtual-0)" -eq 1
    check "reapplyWindowGeometry calls" "$(ncalls reapplyWindowGeometry)" -eq 0
    check "m1's frame after the cancel" "$(rect "$A1")" = "$(rect "$TILE")"
    check "m1 maximized after the cancel" "$(maxof "$A1")" = 3
    check "m1 floating after the cancel" "$(floating "org.pz.plain|$M1")" = false
    check "daemon float sends for m1" "$(floatsends "$M1")" -eq 0
    ;;
bsp)
    assign Virtual-0 "autotile:bsp"
    sleep 2
    RID="{11111111-2222-4333-8444-55555f101501}"
    R='{"actions":[{"value":true,"type":"setHideTitleBar"}],"enabled":true,"id":"'"$RID"'","match":{"all":[{"field":"appId","op":"equals","value":"org.pz.plain"},{"field":"isTiled","op":"equals","value":true}]},"name":"probe F1015 tiled look","priority":50}'
    [ "$(PZ org.plasmazones.Rules.addRule "$R")" = true ] || stage_fail "Rules.addRule refused the tiled-look rule"
    on_exit "PZ org.plasmazones.Rules.removeRule '$RID' >/dev/null"
    sleep 1
    open t1
    open t2
    ids
    T1=$(uuid t1)
    T2=$(uuid t2)
    [ -n "$T1" ] && [ -n "$T2" ] || stage_fail "t1 or t2 never mapped"
    G1=$(geo t1)
    echo "staged: t1 $G1 | t2 $(geo t2)"
    [ "$(nbof "$G1")" = true ] || stage_fail "the tiled-look rule did not hide t1's title bar while tiled"
    echo "== Escape on the dragged tile t1"
    gesture t1 escape
    cancelled "$T1"
    A1=$(geo t1)
    echo "  t1 $A1 | t2 $(geo t2)"
    check "retile calls naming Virtual-0" "$(ncalls retile Virtual-0)" -eq 1
    check "t1's frame after the cancel" "$(rect "$A1")" = "$(rect "$G1")"
    check "t1's tiled look (noBorder) after the cancel" "$(nbof "$A1")" = true
    check "t1 floating after the cancel" "$(floating "org.pz.plain|$T1")" = false
    echo "== control 1: t1 dragged and dropped (no Escape)"
    PZ org.plasmazones.Tiling.retile Virtual-0 >/dev/null
    sleep 2
    gesture t1 drop
    echo "  t1 $(geo t1)"
    check "control 1: retile calls after a plain drop" "$(ncalls retile)" -eq 0
    check "control 1: reapplyWindowGeometry calls after a plain drop" "$(ncalls reapplyWindowGeometry)" -eq 0
    echo "== control 2: t2 floated before the drag, then the Escape gesture"
    activate t2
    shortcut toggle_window_float
    sleep 2
    if [ "$(floating "org.pz.plain|$T2")" != true ]; then
        note "t2 did not float through toggle_window_float, so control 2 is skipped"
    else
        gesture t2 escape
        echo "  t2 $(geo t2)"
        check "control 2: retile calls after cancelling a floating window's drag" "$(ncalls retile)" -eq 0
        check "control 2: reapplyWindowGeometry calls" "$(ncalls reapplyWindowGeometry)" -eq 0
    fi
    ;;
scroll)
    assign Virtual-0 "scrolling:"
    sleep 2
    open c1
    open c2
    ids
    read -r SX _ SW _ <<< "$(scr Virtual-0)"
    S=""
    for c in c1 c2; do
        read -r x _ w _ <<< "$(geo "$c")"
        [ -n "${x:-}" ] || continue
        if [ -z "$S" ] && [ "${x%.*}" -ge "$SX" ] && [ $((${x%.*} + ${w%.*})) -le $((SX + SW)) ]; then
            S="$c"
        fi
    done
    [ -n "$S" ] || stage_fail "no strip tile wholly inside Virtual-0 (c1 $(geo c1) | c2 $(geo c2))"
    SU=$(uuid "$S")
    G=$(geo "$S")
    echo "staged: c1 $(geo c1) | c2 $(geo c2); subject $S"
    echo "== Escape on the dragged strip tile $S"
    gesture "$S" escape
    cancelled "$SU"
    A=$(geo "$S")
    echo "  $S $A"
    check "reapplyWindowGeometry calls naming $S" "$(ncalls reapplyWindowGeometry "$SU")" -eq 1
    check "retile calls" "$(ncalls retile)" -eq 0
    check "$S's frame after the cancel" "$(rect "$A")" = "$(rect "$G")"
    ;;
*) stage_fail "unknown PZ_F1015_RUN '$RUN'" ;;
esac
finish
