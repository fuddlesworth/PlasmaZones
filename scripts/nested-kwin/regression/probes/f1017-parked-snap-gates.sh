#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: GPL-3.0-or-later
#
# f1017-parked-snap-gates: a parked snap context comes back only where snapping still places.
# Runner: 2 outputs, no flags; PZ_F1017_RUN=disabled (row f1017-parked-snap-gates-disabled) or
#   control (row f1017-parked-snap-gates-control).
# Staging: Virtual-0 and Virtual-1 COLS3, Snap Assist off. p1 sent to Virtual-1 and snapped into
#   its zone 1. Virtual-1 is disabled (KWin moves p1 to Virtual-0, snap parks the context). While it
#   is away: disabled = snappingDisabledDesktops ['Virtual-1/1']; control = nothing. Virtual-1 is
#   enabled again (KWin returns p1 untouched).
# Asserts disabled: no "SnapEngine::readoptParked: <p1> re-seated" line and p1 has no zone.
# Asserts control: one re-seat line and p1 back in its zone 1.
# A FAIL means (disabled): a returning output re-seated a zone in a context switched off.
set -u
NEST="$1"
LOG="$2"
. "$(dirname "$0")/../lib.sh"
RUN="${PZ_F1017_RUN:-control}"
case "$RUN" in disabled | control) ;; *) stage_fail "unknown PZ_F1017_RUN '$RUN'" ;; esac
kwjs_ok
need_outputs 2
on_exit 'ksd output.Virtual-1.enable >/dev/null; gsetting snappingDisabledDesktops "<@as []>" >/dev/null'
setting snapAssistEnabled false
assign Virtual-0 "$COLS3"
assign Virtual-1 "$COLS3"
sleep 2
open p1
sendto p1 Virtual-1 || stage_fail "p1 not on Virtual-1 ($(geo p1))"
ids
P1U=$(uuid p1)
[ -n "$P1U" ] || stage_fail "p1 never mapped"
P1="org.pz.plain|$P1U"
PZ org.plasmazones.Control.snapWindowToZone "$P1" 1 Virtual-1 >/dev/null
sleep 2
Z1=$(zoneof "$P1")
echo "staged: p1 $(geo p1) zone=$Z1"
case "$Z1" in "{"*) ;; *) stage_fail "p1 did not snap on Virtual-1 ($Z1)" ;; esac
echo "== Virtual-1 disabled"
dmark
ksd output.Virtual-1.disable
sleep 4
echo "  p1 $(geo p1)"
[ -n "$(dlog "SnapEngine::parkOutput: [^ ]*Virtual-1.*")" ] || stage_fail "snap parked nothing for Virtual-1"
if [ "$RUN" = disabled ]; then
    gsetting snappingDisabledDesktops "<['Virtual-1/1']>" >/dev/null
fi
sleep 2
echo "== Virtual-1 enabled again"
dmark
ksd output.Virtual-1.enable
sleep 5
echo "  p1 $(geo p1) | settle rows for p1: $(dlog "reportOutputSettle: [^ ]*$P1U.*" | grep -o "verdict [0-9]*" | tr '\n' ' ')"
[ "$(outof "$(geo p1)")" = Virtual-1 ] || stage_fail "KWin did not return p1 to Virtual-1"
RS=$(dlog "SnapEngine::readoptParked: [^ ]*$P1U.*" | grep -c "re-seated")
if [ "$RUN" = control ]; then
    check "re-seat lines for p1" "$RS" -eq 1
    check "p1's zone after the return" "$(zoneof "$P1")" = "$Z1"
else
    check "re-seat lines for p1 in the disabled context" "$RS" -eq 0
    check "p1's zone after the return" "$(zoneof "$P1")" = ""
fi
finish
