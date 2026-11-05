#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: GPL-3.0-or-later
#
# f1108-bus-float-hidden-refused: a bus caller other than the compositor bridge cannot float a
# window on a hidden desktop through setWindowFloatingForScreen; the effect's own floats still
# work. Runner: 1 output, no flags.
# Staging: two desktops, both autotile:bsp on Virtual-0. On desktop 2 h opens and tiles. Desktop 1
#   is shown and d1, d2 open there and tile. From this shell (gdbus, not the effect's connection)
#   WindowTracking.setWindowFloatingForScreen(h, Virtual-0, true); then desktop 2 is shown. Control:
#   back on desktop 1 d2 is minimized, which the effect floats over the same verb.
# Asserts: one "setWindowFloatingForScreen: <h> ... - refused for" line and no floating= true send
#   for h, h not floating and still at its tile on desktop 2; the minimize of d2 logs a floating=
#   true send and no refusal.
# A FAIL means: desktop 1's engine adopted the hidden window floating at a foreign caller's word.
set -u
NEST="$1"
LOG="$2"
. "$(dirname "$0")/../lib.sh"
kwjs_ok
setting snapAssistEnabled false
newdesk 1 D2
assignd Virtual-0 1 "autotile:bsp"
assignd Virtual-0 2 "autotile:bsp"
sleep 1.5
desk 2
open h
sleep 1.5
desk 1
open d1
open d2
sleep 2
ids
HU=$(uuid h)
D2U=$(uuid d2)
[ -n "$HU" ] && [ -n "$D2U" ] || stage_fail "h and d2 did not both map"
H="org.pz.plain|$HU"
GH=$(geo h)
echo "staged: h $GH (desktop 2) | d1 $(geo d1) | d2 $(geo d2) | desktop $(curdesk)"
[ "$(deskof "$GH")" = 2 ] || stage_fail "h is not on desktop 2 alone ($GH)"
echo "== bus float of h from this shell"
dmark
echo "  reply: $(gdbus call --session --dest org.plasmazones --object-path /PlasmaZones --method org.plasmazones.WindowTracking.setWindowFloatingForScreen "$H" Virtual-0 true 2>&1 | tr -d '\n')"
sleep 2
dlog "setWindowFloatingForScreen: [^ ]*$HU.*|setWindowFloatingForScreen: windowId= [^ ]*$HU.*" | sed 's/org.pz.plain|[^ ]*/<h>/; s/^/    /'
check "refusal lines for h" "$(dlog "setWindowFloatingForScreen: [^ ]*$HU.*refused for" | wc -l)" -eq 1
check "floating= true sends for h" "$(floatsends "$HU")" -eq 0
check "isWindowFloating h" "$(floating "$H")" = false
echo "== desktop 2 shown"
desk 2
sleep 2
AH=$(geo h)
echo "  h $AH"
check "h still at its tile" "$(rect "$AH")" = "$(rect "$GH")"
echo "== control: desktop 1, d2 minimized (the effect's own float)"
desk 1
dmark
on d2 'w[i].minimized = true;'
sleep 2
check "floating= true sends for the minimized d2" "$(floatsends "$D2U")" -ge 1
check "refusal lines for d2" "$(dlog "setWindowFloatingForScreen: [^ ]*$D2U.*refused" | wc -l)" -eq 0
finish
