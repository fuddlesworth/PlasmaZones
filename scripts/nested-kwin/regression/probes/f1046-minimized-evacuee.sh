#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: GPL-3.0-or-later
#
# f1046-minimized-evacuee: a minimized evacuee KWin returns keeps its tile (F1046), and a minimized
# snap-float evacuee floats where it landed, and only there (F1047).
# Runner: 2 outputs, no flags; PZ_F1046_RUN=tile (row f1046-minimized-evacuee-tile) or snap
#   (row f1046-minimized-evacuee-snap).
# Staging tile: Virtual-0 and Virtual-1 autotile:bsp, w1 and w2 tiled on Virtual-1, w1 minimized.
#   Virtual-1 is disabled (w1 evacuated while minimized, settle verdict 2), enabled again (KWin
#   returns w1 untouched, settle verdict 1 Readopt), then w1 is unminimized.
# Staging snap: Virtual-0 and Virtual-1 COLS3, Snap Assist off. w1 snapped into Virtual-1's zone 1,
#   floated with the float shortcut, minimized. Virtual-1 is disabled (w1 lands on Virtual-0) and
#   left off; w1 is unminimized there; then Virtual-0 is switched to autotile:bsp.
# Asserts tile: from the return on, no floating= true send for w1 and at least one floating= false
#   send, w1 not floating, Virtual-1 lists two tiles with w1 not floating.
# Asserts snap: one "SnapEngine::handoffReceive: <w1> ... wasFloating= true" from the disable on,
#   w1 floating with no zone on Virtual-0, and no floating= true send for w1 after the switch to
#   autotile (no stale pending float).
# A FAIL means: the unminimize took a stale pending float (tile), or the snap float was not stated
#   at the settle and lingered for the tiling engine (snap).
set -u
NEST="$1"
LOG="$2"
. "$(dirname "$0")/../lib.sh"
RUN="${PZ_F1046_RUN:-tile}"
kwjs_ok
need_outputs 2
on_exit 'ksd output.Virtual-1.enable >/dev/null'
setting snapAssistEnabled false

case "$RUN" in
tile)
    assign Virtual-0 "autotile:bsp"
    assign Virtual-1 "autotile:bsp"
    sleep 2
    for c in w1 w2; do
        open "$c"
        sendto "$c" Virtual-1 || stage_fail "$c not on Virtual-1 ($(geo "$c"))"
    done
    sleep 1.5
    ids
    W1U=$(uuid w1)
    [ -n "$W1U" ] || stage_fail "w1 never mapped"
    W1="org.pz.plain|$W1U"
    echo "staged: w1 (...${W1U: -8}) $(geo w1) | w2 $(geo w2) | Virtual-1 tiles $(tiles Virtual-1)"
    case "$(tiles Virtual-1)" in 2*) ;; *) stage_fail "Virtual-1 does not hold two tiles" ;; esac
    on w1 'w[i].minimized = true;'
    sleep 2
    echo "w1 minimized: $(geo w1) | floating=$(floating "$W1") | Virtual-1 tiles $(tiles Virtual-1)"
    echo "== Virtual-1 disabled"
    dmark
    ksd output.Virtual-1.disable
    sleep 4
    echo "  w1 $(geo w1) | w2 $(geo w2)"
    settlerows "$W1U"
    [ "$(verdictof "$W1U")" = 2 ] || stage_fail "w1's disable row is not verdict 2 ($(verdictof "$W1U"))"
    echo "== Virtual-1 enabled again (KWin returns w1 untouched)"
    dmark
    ksd output.Virtual-1.enable
    sleep 5
    echo "  w1 $(geo w1) | w2 $(geo w2) | Virtual-1 tiles $(tiles Virtual-1)"
    settlerows "$W1U"
    V=$(verdictof "$W1U")
    [ "$V" = 1 ] || stage_fail "w1's enable row is not verdict 1 Readopt (${V:-no row}); F1046 is not exercised"
    echo "== w1 unminimized"
    on w1 'w[i].minimized = false;'
    sleep 3
    echo "  w1 $(geo w1) | w2 $(geo w2)"
    dlog "setWindowFloatingForScreen: windowId= [^ ]*$W1U.*" | sed 's/org.pz.plain|[^ ]*/<w1>/; s/^/    /'
    check "floating= true sends for w1 since the return" "$(floatsends "$W1U" true)" -eq 0
    check "floating= false sends for w1 since the return" "$(floatsends "$W1U" false)" -ge 1
    check "w1 floating after the unminimize" "$(floating "$W1")" = false
    check "Virtual-1 tiles, w1 among them not floating" "$(tiles Virtual-1)" '~' "^2 .*'${W1U: -8}'"
    ;;
snap)
    assign Virtual-0 "$COLS3"
    assign Virtual-1 "$COLS3"
    sleep 2
    open w1
    sendto w1 Virtual-1 || stage_fail "w1 not on Virtual-1 ($(geo w1))"
    ids
    W1U=$(uuid w1)
    [ -n "$W1U" ] || stage_fail "w1 never mapped"
    W1="org.pz.plain|$W1U"
    PZ org.plasmazones.Control.snapWindowToZone "$W1" 1 Virtual-1 >/dev/null
    sleep 2
    activate w1
    shortcut toggle_window_float
    sleep 2
    echo "staged: w1 (...${W1U: -8}) $(geo w1) floating=$(floating "$W1") zone='$(zoneof "$W1")'"
    [ "$(floating "$W1")" = true ] || stage_fail "w1 is not snap-floating on Virtual-1"
    on w1 'w[i].minimized = true;'
    sleep 2
    echo "== Virtual-1 disabled and left off"
    dmark
    ksd output.Virtual-1.disable
    sleep 4
    echo "  w1 $(geo w1)"
    settlerows "$W1U"
    [ "$(outof "$(geo w1)")" = Virtual-0 ] || stage_fail "w1 was not evacuated to Virtual-0"
    echo "== w1 unminimized on Virtual-0"
    on w1 'w[i].minimized = false;'
    sleep 3
    echo "  w1 $(geo w1)"
    # Counted from the disable on: the snap float is stated at the settle (verdict 2), not at the unminimize.
    handoffs "$W1U" | sed 's/^/    /'
    check "handoffReceive to Virtual-0 wasFloating= true for w1 since the disable" \
        "$(handoffs "$W1U" | grep -c 'to "Virtual-0".*wasFloating= true')" -eq 1
    check "w1 floating on Virtual-0" "$(floating "$W1")" = true
    check "w1's zone on Virtual-0" "$(zoneof "$W1")" = ""
    echo "== Virtual-0 switched to autotile:bsp"
    dmark
    emark
    assign Virtual-0 "autotile:bsp"
    sleep 3
    ANN=$(elog "Notified autotile: (windowOpened|windowReannounced) [^ ]*$W1U|Notified autotile: windowsOpenedBatch" | wc -l)
    [ "$ANN" -gt 0 ] || stage_fail "no autotile announce after the switch; the stale-entry half is not exercised"
    echo "  w1 $(geo w1) floating=$(floating "$W1") | Virtual-0 tiles $(tiles Virtual-0)"
    check "floating= true sends for w1 after the switch to autotile" "$(floatsends "$W1U" true)" -eq 0
    ;;
*) stage_fail "unknown PZ_F1046_RUN '$RUN'" ;;
esac
finish
