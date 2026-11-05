#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: GPL-3.0-or-later
#
# f1028-parked-strip-float: a window parked floating on a scrolling output comes back floating where
# KWin returned it (F1028), also with its desktop out of view at the return (F1095).
# Runner: 2 outputs, no flags; PZ_F1028_RUN=inview (row f1028-parked-strip-float-inview) or hidden
#   (row f1028-parked-strip-float-hidden).
# Staging: Virtual-1 scrolling, Virtual-0 COLS3, Snap Assist off. On Virtual-1 c1 and c2 open as
#   columns and f opens, is floated (toggle_window_float) and moved to a spot S in the lower right.
#   hidden: two desktops, all of it on desktop 2, then desktop 1 shown before the unplug. Virtual-1
#   is disabled (KWin evacuates to Virtual-0), nothing is touched, Virtual-1 is enabled again (KWin
#   returns them); hidden: desktop 2 is shown after the return.
# Asserts: no Tiling.windowsTileRequested batch names f from the return on (hidden: also after
#   desktop 2 is shown), f floating at S, the strip "c1 | c2".
# A FAIL means: the parked float took a column on its return.
set -u
NEST="$1"
LOG="$2"
. "$(dirname "$0")/../lib.sh"
RUN="${PZ_F1028_RUN:-inview}"
kwjs_ok
need_outputs 2
read -r BX BY BW BH <<< "$(scr Virtual-1)"
on_exit 'ksd output.Virtual-1.enable >/dev/null'
setting snapAssistEnabled false
assign Virtual-0 "$COLS3"
assign Virtual-1 "scrolling:"
sleep 2
case "$RUN" in
inview) ;;
hidden)
    newdesk 1 D2
    desk 2
    ;;
*) stage_fail "unknown PZ_F1028_RUN '$RUN'" ;;
esac
echo "desktop $(curdesk)"
for c in c1 c2 f; do
    open "$c"
    sendto "$c" Virtual-1 || stage_fail "$c not on Virtual-1 ($(geo "$c"))"
done
sleep 1.5
ids
FU=$(uuid f)
[ -n "$FU" ] || stage_fail "f never mapped"
F="org.pz.plain|$FU"
activate f
shortcut toggle_window_float
sleep 2
moveto f $((BX + BW - 460)) $((BY + BH - 380))
sleep 1.5
S=$(geo f)
echo "staged: c1 $(geo c1) | c2 $(geo c2) | f $S floating=$(floating "$F") | strip $(striporder Virtual-1)"
[ "$(floating "$F")" = true ] || stage_fail "f is not floating"
[ "$(striporder Virtual-1)" = "c1 | c2" ] || stage_fail "the strip is not c1 | c2 before the unplug"
if [ "$RUN" = hidden ]; then
    desk 1
    echo "desktop $(curdesk) shown (desktop 2's strip out of view)"
fi
MON="$NEST/f1028-monitor.log"
mon_start "$MON" "type='signal',interface='org.plasmazones.Tiling',member='windowsTileRequested'"
echo "== Virtual-1 disabled"
dmark
ksd output.Virtual-1.disable
sleep 4
echo "  f $(geo f) | c1 $(geo c1)"
[ -n "$(dlog "ScrollEngine::parkOutput: .*")" ] || stage_fail "the scroll engine parked nothing for Virtual-1"
echo "== Virtual-1 enabled again"
mmark "$MON"
ksd output.Virtual-1.enable
sleep 5
dlog "ScrollEngine::readoptParked: .*" | sed 's/org.pz.plain|//g; s/^/    /'
if [ "$RUN" = hidden ]; then
    check "batches naming f before desktop 2 is shown" "$(msgs "$MON" | grep "^windowsTileRequested" | grep -c "$FU")" -eq 0
    desk 2
    sleep 1.5
    echo "  desktop $(curdesk) shown"
fi
A=$(geo f)
echo "  f $A floating=$(floating "$F") | c1 $(geo c1) | c2 $(geo c2)"
check "windowsTileRequested batches naming f since the return" "$(msgs "$MON" | grep "^windowsTileRequested" | grep -c "$FU")" -eq 0
check "f floating" "$(floating "$F")" = true
check "f at its spot S" "$(rect "$A")" = "$(rect "$S")"
check "the strip" "$(striporder Virtual-1)" = "c1 | c2"
finish
