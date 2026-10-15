#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: GPL-3.0-or-later
#
# f884-reopen-restores-zone: the control for f884-drop-beats-late-restore. A reopened window that
# nothing drags still takes its remembered zone, so the drop mark voids only a reply a drop
# followed. Runner: 1 output, flag F.
# Staging: Virtual-0 COLS3, Snap Assist off. s1 is snapped into zone 1 with an Alt drag and closed;
#   s2 (same app) then opens with nothing touching it.
# Asserts: the restore reply snaps s2, none is superseded, and s2 sits at zone 1's rect.
# A FAIL means: the F884 drop mark swallowed a restore no drop followed.
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
open s2 300 300
sleep 1
elog '"resolveWindowRestore" .*' | sed 's/^/    /'
check "restore replies snapping s2" "$(elog '"resolveWindowRestore" snapping' | wc -l)" -ge 1
check "restore replies superseded" "$(elog '"resolveWindowRestore" superseded' | wc -l)" -eq 0
check "s2 reopened untouched sits at zone 1" "$(rect "$(geo s2)")" = "$Z1"
finish
