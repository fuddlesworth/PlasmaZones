#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: GPL-3.0-or-later
#
# f680-refused-maximize-counter: a maximize KWin refuses takes no claim, so the strip still
# counters an external move afterwards. Runner: 1 output, flag X.
# Staging: Virtual-0 scrolling. fx is an X11 client that is not resizable (fixed_gtk.py, min ==
#   max, so KWin's maximizable is false), adopted into the strip. Scrolling.toggleMaximizeToEdges
#   on fx (KWin refuses the maximize), then a KWin script moves fx 60 px right and 40 px down, out
#   of any PlasmaZones apply.
# Asserts: the refused maximize leaves fx unmaximized, the script move is countered ("Countering
#   external move of scroll-managed window" for fx) and fx is back at its column rect.
# A FAIL means: the claim over a maximize KWin never set kept fx inside the counter-assert's
#   maximize-transition exemption, so fx stayed where the script put it.
set -u
NEST="$1"
LOG="$2"
. "$(dirname "$0")/../lib.sh"
kwjs_ok
assign Virtual-0 "scrolling:"
sleep 2
xclient fixed_gtk.py fx 420 320
sleep 1
ids
FX=$(uuid fx)
[ -n "$FX" ] || stage_fail "fx never mapped"
FXID="org.pz.plain|$FX"
counters() { elog "Countering external move of scroll-managed window" | grep -c "$FX"; }
echo "fx in the strip: $(geo fx) maximizable=$(wprop fx 'w.maximizable')"
[ "$(wprop fx 'w.maximizable')" = false ] || stage_fail "fx is maximizable, so KWin would not refuse the maximize"
emark
echo "toggleMaximizeToEdges: $(PZ org.plasmazones.Scrolling.toggleMaximizeToEdges Virtual-0 "$FXID")"
sleep 2.5
COL=$(geo fx)
echo "fx after the refused maximize: $COL"
check "fx's maximize mode after the refused maximize" "$(maxof "$COL")" = 0
note "counters during the maximize: $(counters)"
read -r CX CY _ <<< "$COL"
CX=${CX%.*}
CY=${CY%.*}
emark
moveto fx $((CX + 60)) $((CY + 40))
sleep 1.5
elog "Countering external move.*$FX" | head -2 | sed 's/^/    /'
check "counter lines for fx after the script move" "$(counters)" -ge 1
check "fx's origin after the script move (its column rect)" "$(geo fx | awk '{ print $1, $2 }')" = "$CX $CY"
finish
