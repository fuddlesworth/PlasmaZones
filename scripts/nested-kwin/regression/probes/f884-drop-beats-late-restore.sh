#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: GPL-3.0-or-later
#
# f884-drop-beats-late-restore: a window dropped by hand before its restore reply arrives stays
# where it was dropped. Runner: 1 output, flag F.
# Staging: Virtual-0 COLS3, Snap Assist off. s1 is snapped into zone 1 with an Alt drag and closed,
#   so the app keeps a remembered zone. The daemon is frozen, s2 opens (its resolveWindowRestore
#   waits on the frozen daemon), s2 is Meta-dragged to a free spot and dropped, and the daemon is
#   let go.
# Asserts: the effect logs the restore reply as superseded by the drop and applies no zone snap for
#   s2, and s2 stays where it was dropped.
# A FAIL means: the late restore reply pulled the dropped window back into zone 1.
set -u
NEST="$1"
LOG="$2"
. "$(dirname "$0")/../lib.sh"
kwjs_ok
setting snapAssistEnabled false
assign Virtual-0 "$COLS3"
sleep 2
open s1 300 300
moveto s1 600 300
sleep 1
read -r GX GY <<< "$(centre s1)"
fake abs "$GX" "$GY" wait 100 keydown 125 wait 50 down left wait 150 \
    keydown 56 wait 150 path "$GX" "$GY" 170 384 20 30 wait 900 up left wait 80 keyup 56 keyup 125
sleep 2
ids
G=$(geo s1)
Z=$(zoneof "org.pz.plain|$(uuid s1)")
echo "s1 snapped: $G zone=$Z"
[ "$Z" = "$(zoneid "$COLS3" 1)" ] || stage_fail "s1 did not snap into COLS3 zone 1 ($G, zone $Z)"
Z1=$(rect "$G")
on s1 'w[i].closeWindow();'
sleep 2
emark
dstop || stage_fail "the daemon could not be frozen"
echo "daemon frozen"
open s2 300 300
echo "s2 opened under the frozen daemon: $(geo s2)"
read -r GX GY <<< "$(centre s2)"
fake abs "$GX" "$GY" wait 100 keydown 125 wait 50 down left wait 150 \
    path "$GX" "$GY" 820 560 20 30 wait 500 up left wait 80 keyup 125
sleep 1
DROPPED=$(rect "$(geo s2)")
echo "s2 dropped under the frozen daemon: $DROPPED"
[ -n "$DROPPED" ] && [ "$DROPPED" != "$Z1" ] || stage_fail "s2 was not dropped off zone 1 ($DROPPED)"
dcont
echo "daemon running again"
sleep 3
elog '"resolveWindowRestore" .*|size-only restore for .*' | sed 's/^/    /'
check "restore replies superseded by the drop" \
    "$(elog '"resolveWindowRestore" superseded by a drop of' | wc -l)" -ge 1
check "restore replies snapping s2 into a zone" "$(elog '"resolveWindowRestore" snapping' | wc -l)" -eq 0
check "s2 after the replies" "$(rect "$(geo s2)")" = "$DROPPED"
finish
