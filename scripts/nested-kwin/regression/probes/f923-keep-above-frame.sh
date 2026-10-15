#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: GPL-3.0-or-later
#
# f923-keep-above-frame: a free kept-above window keeps the daemon's frame current, so
# snap-then-float returns it where it was last moved, and its float resize records the new size
# (F923); an Exclude-matched window still reports nothing. Runner: 1 output, flags F S.
# Staging: snapping Virtual-0, Snap Assist off, an Exclude rule on app org.pz.e18excl. p1 =
#   plain_gtk.py e18p1, p2 = appid_gtk.py e18p2 org.pz.e18excl. A dbus-monitor records
#   setFrameGeometry and storePreTileGeometry. p1 is put at 200,150 by script, kept above, then
#   moved by script to 900,300; it is snapped into zone 1 and floated (Snap.setWindowFloat). p2 is
#   moved by script to 700,450 and closed. Last a Meta+right drag resizes the floating p1.
# Asserts: one setFrameGeometry for p1 at 900 300 after the keep-above move; p1 floats back to
#   900,300; no setFrameGeometry for p2; one storePreTileGeometry for p1 after the resize, carrying
#   p1's new frame.
# A FAIL means: the kept-above window's moves never reached the daemon, so its float-back used a
#   stale frame.
set -u
NEST="$1"
LOG="$2"
. "$(dirname "$0")/../lib.sh"
kwjs_ok
setting snapAssistEnabled false
RID="{11111111-2222-4333-8444-5555555e18b1}"
RULE='{"actions":[{"params":{},"type":"exclude"}],"enabled":true,"id":"'"$RID"'","match":{"all":[{"field":"appId","op":"equals","value":"org.pz.e18excl"}]},"name":"probe F923 exclude","priority":50}'
[ "$(PZ org.plasmazones.Rules.addRule "$RULE")" = true ] || stage_fail "Rules.addRule refused the exclude rule"
on_exit "PZ org.plasmazones.Rules.removeRule '$RID' >/dev/null"
sleep 1.5
open e18p1 400 300
openapp e18p2 org.pz.e18excl 300 300
ids
P1U=$(uuid e18p1)
P2U=$(uuid e18p2)
[ -n "$P1U" ] && [ -n "$P2U" ] || stage_fail "p1 or p2 never mapped ($P1U / $P2U)"
P1="org.pz.plain|$P1U"
MON="$NEST/e18b-monitor.log"
mon_start "$MON" "type='method_call',interface='org.plasmazones.WindowTracking',member='setFrameGeometry'" \
    "type='method_call',interface='org.plasmazones.WindowTracking',member='storePreTileGeometry'"
# calls MEMBER UUID -> the MEMBER calls naming UUID since mmark, arguments after the id
calls() { msgs "$MON" | awk -v m="$1" -v u="$2" '$1 == m && index($2, u) { $1 = ""; $2 = ""; sub(/^ +/, ""); print }'; }
moveto e18p1 200 150
sleep 1.5
echo "p1 at 200,150: $(geo e18p1)"
on e18p1 "w[i].keepAbove = true;"
sleep 1
[ "$(wprop e18p1 'w.keepAbove')" = true ] || stage_fail "p1 is not kept above"
mmark "$MON"
moveto e18p1 900 300
sleep 1.5
echo "p1 kept above and moved to 900,300: $(geo e18p1)"
[ "$(rect "$(geo e18p1)" | cut -d' ' -f1-2)" = "900 300" ] || stage_fail "the script move of p1 did not land ($(geo e18p1))"
check "setFrameGeometry calls for p1 after the keep-above move" "$(calls setFrameGeometry "$P1U" | wc -l)" -eq 1
check "p1's reported frame after the keep-above move" "$(calls setFrameGeometry "$P1U" | tail -1 | cut -d' ' -f1-2)" = "900 300"
PZ org.plasmazones.Control.snapWindowToZone "$P1" 1 Virtual-0 >/dev/null
sleep 2.5
Z=$(zoneof "$P1")
echo "p1 snapped into zone 1: $(geo e18p1) zone=$Z"
case "$Z" in "{"*) ;; *) stage_fail "the keep-above p1 was not snapped ($Z)" ;; esac
PZ org.plasmazones.Snap.setWindowFloat "$P1" true >/dev/null
sleep 2.5
G=$(geo e18p1)
echo "p1 floated: $G floating=$(floating "$P1")"
check "p1's float-back position" "$(rect "$G" | cut -d' ' -f1-2)" = "900 300"
echo "== Exclude control"
mmark "$MON"
moveto e18p2 700 450
sleep 1.5
echo "p2 moved: $(geo e18p2)"
check "setFrameGeometry calls for the excluded p2" "$(calls setFrameGeometry "$P2U" | wc -l)" -eq 0
on e18p2 "w[i].closeWindow();"
sleep 1.5
echo "== float resize of the kept-above p1"
read -r X Y W H <<< "$(rect "$G")"
CX=$((${X%.*} + ${W%.*} / 2))
CY=$((${Y%.*} + ${H%.*} / 2))
mmark "$MON"
# KEY_LEFTMETA = 125: Meta+right drag from the centre is KWin's modifier resize.
fake abs "$CX" "$CY" wait 100 keydown 125 wait 50 down right wait 100 \
    path "$CX" "$CY" $((CX + 120)) $((CY + 80)) 15 20 wait 150 up right wait 50 keyup 125
sleep 1.5
G2=$(geo e18p1)
echo "p1 after the resize: $G2"
[ "$(size "$G2")" != "$(size "$G")" ] || stage_fail "the modifier resize did not change p1's size ($G2)"
check "storePreTileGeometry calls for p1 after the resize" "$(calls storePreTileGeometry "$P1U" | wc -l)" -eq 1
check "the stored frame after the resize" "$(calls storePreTileGeometry "$P1U" | tail -1 | cut -d' ' -f1-4)" = "$(rect "$G2")"
finish
