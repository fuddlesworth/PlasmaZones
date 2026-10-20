#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: GPL-3.0-or-later
#
# f1078-fullscreen-monocle-leaves: a fullscreen Monocle tile moved to a snapping desktop gets its size
# back when it leaves fullscreen. Runner: 1 output, no flags.
# Staging: two desktops on Virtual-0, desktop 1 autotile:monocle, desktop 2 COLS3, Snap Assist off.
#   REF is the free size of a throwaway 400x300 plain_gtk.py window r0 opened on desktop 2. m2 then m1
#   opened on desktop 1 (both max=3: Monocle maximizes only with two or more tiles). Then
#   m1.fullScreen = true, m1 moved to desktop 2, desktop 2 shown, m1.fullScreen = false.
# Asserts: m1 is max=0 at REF's size after it leaves fullscreen.
# A FAIL means: m1 stayed KWin-maximized on desktop 2 with no Monocle ledger left to undo it.
set -u
NEST="$1"
LOG="$2"
. "$(dirname "$0")/../lib.sh"
kwjs_ok
setting snapAssistEnabled false
newdesk 1 D2
assignd Virtual-0 1 "autotile:monocle"
assignd Virtual-0 2 "$COLS3"
sleep 1.5
desk 2
open r0
REF=$(size "$(geo r0)")
echo "REF (a free 400x300 plain_gtk.py frame on desktop 2): $REF"
{ [ -n "$REF" ] && [ "$REF" != "x" ]; } || stage_fail "r0 never mapped"
on r0 'w[i].closeWindow();'
sleep 1
desk 1
open m2
open m1
sleep 1.5
echo "staged on desktop $(curdesk): m1 $(geo m1) | m2 $(geo m2)"
[ "$(maxof "$(geo m1)")" = 3 ] || stage_fail "m1 is not Monocle-maximized"
activate m1
on m1 'w[i].fullScreen = true;'
sleep 2
echo "m1 fullscreen: $(geo m1)"
todesk m1 2
sleep 2.5
echo "m1 moved to desktop 2: $(geo m1)"
[ "$(deskof "$(geo m1)")" = 2 ] || stage_fail "m1 did not move to desktop 2"
desk 2
on m1 'w[i].fullScreen = false;'
sleep 2.5
G=$(geo m1)
echo "m1 after leaving fullscreen: $G"
check "m1 maximizeMode after leaving fullscreen" "$(maxof "$G")" = 0
check "m1 size after leaving fullscreen (its pre-Monocle size)" "$(size "$G")" = "$REF"
finish
