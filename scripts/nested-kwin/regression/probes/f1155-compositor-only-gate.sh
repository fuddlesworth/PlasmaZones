#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: GPL-3.0-or-later
#
# f1155-compositor-only-gate: the daemon's compositor-only D-Bus methods admit the registered KWin
# effect and refuse any other caller (#1155). Runner: 1 output, no flags.
# Staging: Virtual-0 scrolling with two Wayland tiles s1 and s2. The daemon is restarted, and the
#   effect registers again and replays its state.
# Asserts: across the session and the restart the daemon logs no refusal of a compositor report,
#   and the effect logs no failed call to one, so the real bridge is always admitted; the restarted
#   daemon still knows s1's metadata and tiles both windows. A call from this shell (another bus
#   peer) to Scrolling.reportCommittedSize, WindowTracking.pruneStaleWindows and
#   Tiling.windowClosed is answered with AccessDenied, and s1 and s2 stay tiled.
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

echo "== restart the daemon"
emark
drestart
sleep 4
check "refused compositor reports in the daemon log" "$(grep -c "reserved for the registered compositor bridge" "$NEST/daemon.log")" -eq 0
check "AccessDenied answers in the compositor log" "$(elog "AccessDenied" | wc -l)" -eq 0
check "s1's metadata after the restart" "$(PZ org.plasmazones.WindowTracking.getWindowMetadata "$S1")" '~' "org.pz.plain"
check "Virtual-0's strip holds both windows" "$(striporder Virtual-0)" '~' "s1.*s2|s2.*s1"

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
check "Virtual-0's strip still holds both windows" "$(striporder Virtual-0)" '~' "s1.*s2|s2.*s1"
finish
