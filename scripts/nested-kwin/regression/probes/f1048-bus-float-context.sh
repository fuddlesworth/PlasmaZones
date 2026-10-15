#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: GPL-3.0-or-later
#
# f1048-bus-float-context: the bus WindowTracking.setWindowFloating keeps to the context its screen
# shows. Runner: 1 output, no flags.
# Staging: two desktops on Virtual-0, desktop 1 autotile:bsp (t1, t2 tiled), desktop 2 COLS3 (s1
#   and s2 snapped into zones 1 and 2 while desktop 2 was in view), Snap Assist off.
#   Run 1: desktop 1 in view, setWindowFloating(s1, true).
#   Run 2: desktop 2 in view, snappingDisabledDesktops ['Virtual-0/2'], setWindowFloating(s2, true).
# Asserts: run 1 sends no floating= true for s1, logs the "is not on the desktop" refusal, leaves
#   desktop 1's tiles as they were and s1 in its zone at its zone rect on desktop 2. Run 2 sends no
#   floating= true for s2 and logs the "disabled context" refusal.
# A FAIL means: the engine in view adopted a window from a hidden desktop or a disabled context.
set -u
NEST="$1"
LOG="$2"
. "$(dirname "$0")/../lib.sh"
kwjs_ok
on_exit 'gsetting snappingDisabledDesktops "<@as []>" >/dev/null'
setting snapAssistEnabled false
newdesk 1 D2
assignd Virtual-0 1 "autotile:bsp"
assignd Virtual-0 2 "$COLS3"
sleep 1.5
desk 2
open s1
open s2
ids
S1U=$(uuid s1)
S2U=$(uuid s2)
[ -n "$S1U" ] && [ -n "$S2U" ] || stage_fail "s1 or s2 never mapped"
S1="org.pz.plain|$S1U"
S2="org.pz.plain|$S2U"
PZ org.plasmazones.Control.snapWindowToZone "$S1" 1 Virtual-0 >/dev/null
PZ org.plasmazones.Control.snapWindowToZone "$S2" 2 Virtual-0 >/dev/null
sleep 2
GS1=$(geo s1)
ZS1=$(zoneof "$S1")
echo "s1 staged on desktop 2: $GS1 zone=$ZS1 | s2 $(geo s2) zone=$(zoneof "$S2")"
case "$ZS1" in "{"*) ;; *) stage_fail "s1 did not snap on desktop 2 ($ZS1)" ;; esac
desk 1
open t1
open t2
sleep 1.5
TB=$(tiles Virtual-0)
echo "desktop $(curdesk): t1 $(geo t1) | t2 $(geo t2) | tiles $TB"
case "$TB" in 2*) ;; *) stage_fail "desktop 1 does not hold two tiles" ;; esac

echo "== run 1: setWindowFloating(s1, true) with s1's desktop hidden"
dmark
PZ org.plasmazones.WindowTracking.setWindowFloating "$S1" true >/dev/null
sleep 2
check "floating= true sends for s1" "$(floatsends "$S1U" true)" -eq 0
check "hidden-desktop refusal lines for s1" \
    "$(dlog "setWindowFloating: [^ ]*$S1U[^ ]* is not on the desktop and activity.*refused" | wc -l)" -ge 1
check "Virtual-0 tiles in view" "$(tiles Virtual-0)" = "$TB"
check "s1 floating" "$(floating "$S1")" = false
desk 2
check "s1's zone on desktop 2" "$(zoneof "$S1")" = "$ZS1"
check "s1's frame on desktop 2" "$(rect "$(geo s1)")" = "$(rect "$GS1")"

echo "== run 2: setWindowFloating(s2, true) on a snapping context switched off"
gsetting snappingDisabledDesktops "<['Virtual-0/2']>" >/dev/null
sleep 2
dmark
PZ org.plasmazones.WindowTracking.setWindowFloating "$S2" true >/dev/null
sleep 2
echo "  s2 $(geo s2) floating=$(floating "$S2")"
check "floating= true sends for s2" "$(floatsends "$S2U" true)" -eq 0
check "disabled-context refusal lines for s2" \
    "$(dlog "setWindowFloating: [^ ]*$S2U[^ ]* is in a disabled context.*refused" | wc -l)" -ge 1
finish
