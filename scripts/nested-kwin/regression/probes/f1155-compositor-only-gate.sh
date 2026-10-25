#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: GPL-3.0-or-later
#
# f1155-compositor-only-gate: the daemon's compositor-only D-Bus methods admit the registered KWin
# effect and refuse any other caller (#1155). Runner: 1 output, no flags.
# Staging: Virtual-0 scrolling with two Wayland tiles s1 and s2, both columns of the strip. The
#   daemon is restarted, and the effect registers again and replays its state.
# Asserts: across the session and the restart neither daemon's log holds a refusal of a compositor
#   report, and the compositor log holds no refusal answered to the effect, so the real bridge is
#   always admitted. The restarted daemon still knows s1's metadata and its strip is the one from
#   before the restart. A call from this shell (another bus peer) to Scrolling.reportCommittedSize,
#   WindowTracking.pruneStaleWindows and Tiling.windowClosed is answered with AccessDenied, and the
#   strip is still the same.
# A FAIL means: the gate refused the effect (a report sent before registerBridge, or a bridge name
#   that does not match), or it let a stranger's call through.
set -u
NEST="$1"
LOG="$2"
. "$(dirname "$0")/../lib.sh"
kwjs_ok
assign Virtual-0 "scrolling:"
sleep 2
open s1
open s2
sleep 2
ids
S1=$(uuid s1)
S2=$(uuid s2)
[ -n "$S1" ] && [ -n "$S2" ] || stage_fail "s1 or s2 never mapped"
STRIP=$(striporder Virtual-0)
echo "Virtual-0's strip: $STRIP"
[ "$(instrip s1)" = yes ] && [ "$(instrip s2)" = yes ] || stage_fail "the strip did not take both s1 and s2 ($STRIP)"

echo "== restart the daemon"
drestart
sleep 4
# The daemon logs a refusal as '"MEMBER" refused for ":1.N"'. drestart moved the first daemon's log
# to daemon.log.earlier, and the effect logs a failed call by the error's message.
check "refusals of a compositor report in both daemons' logs" \
    "$(cat "$NEST/daemon.log.earlier" "$NEST/daemon.log" 2>/dev/null | grep -cE 'refused for "?:[0-9]')" -eq 0
check "refusals answered to the effect in the compositor log" \
    "$(elog "reserved for the registered compositor bridge" | wc -l)" -eq 0
check "s1's metadata after the restart" "$(PZ org.plasmazones.WindowTracking.getWindowMetadata "$S1")" '~' "org.pz.plain"
check "Virtual-0's strip after the restart" "$(striporder Virtual-0)" = "$STRIP"

echo "== calls from this shell"
r=$(PZ org.plasmazones.Scrolling.reportCommittedSize "org.pz.plain|$S1" 400 300 3000 300)
echo "  reportCommittedSize: $r"
check "reportCommittedSize from a stranger" "$r" '~' "AccessDenied"
r=$(PZ org.plasmazones.WindowTracking.pruneStaleWindows "x")
echo "  pruneStaleWindows: $r"
check "pruneStaleWindows from a stranger" "$r" '~' "AccessDenied"
r=$(PZ org.plasmazones.Tiling.windowClosed "org.pz.plain|$S2")
echo "  windowClosed: $r"
check "Tiling.windowClosed from a stranger" "$r" '~' "AccessDenied"
sleep 1
check "Virtual-0's strip after the stranger's calls" "$(striporder Virtual-0)" = "$STRIP"
finish
