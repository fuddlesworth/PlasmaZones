#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: GPL-3.0-or-later
#
# f1097-hidden-strip-order: a parked strip whose windows were all returned while their desktop was
# out of view still comes back in order, after one of them was touched. Runner: 2 outputs, flag F.
# Staging: two desktops. Virtual-1 scrolling, Virtual-0 COLS3, Snap Assist off. On desktop 2 a, b,
#   c open on Virtual-1 as columns in that order; desktop 1 is shown. Virtual-1 is disabled (KWin
#   evacuates a, b, c to Virtual-0 on hidden desktop 2). Desktop 2 is shown long enough to drag b
#   by hand on Virtual-0 (a touch: its park is dropped), then desktop 1 again. Virtual-1 is enabled
#   (KWin returns a and c while desktop 2 is out of view). Desktop 2 is shown.
# Asserts: the strip on desktop 2 reads "a | c" and b stays on Virtual-0.
# A FAIL means: the touch erased the stash, so a and c came back as fresh columns in arrival order.
set -u
NEST="$1"
LOG="$2"
. "$(dirname "$0")/../lib.sh"
kwjs_ok
need_outputs 2
on_exit 'ksd output.Virtual-1.enable >/dev/null'
setting snapAssistEnabled false
assign Virtual-0 "$COLS3"
assign Virtual-1 "scrolling:"
newdesk 1 D2
sleep 1.5
desk 2
for c in a b c; do
    open "$c"
    sendto "$c" Virtual-1 || stage_fail "$c not on Virtual-1 ($(geo "$c"))"
done
sleep 1.5
ids
AU=$(uuid a)
CU=$(uuid c)
[ -n "$AU" ] && [ -n "$CU" ] || stage_fail "a or c never mapped"
echo "staged on desktop $(curdesk): strip $(striporder Virtual-1)"
[ "$(striporder Virtual-1)" = "a | b | c" ] || stage_fail "the strip is not a | b | c"
desk 1
echo "== Virtual-1 disabled (desktop $(curdesk) in view)"
dmark
ksd output.Virtual-1.disable
sleep 4
echo "  a $(geo a) | b $(geo b) | c $(geo c)"
[ "$(outof "$(geo b)")" = Virtual-0 ] || stage_fail "b was not evacuated to Virtual-0"
echo "== b dragged by hand on Virtual-0 (desktop 2 shown for it)"
desk 2
activate b
GB=$(geo b)
touchdrag b 120 60
echo "  b $(geo b)"
[ "$(rect "$(geo b)")" != "$(rect "$GB")" ] || stage_fail "the drag did not move b"
desk 1
echo "== Virtual-1 enabled (desktop $(curdesk) in view)"
dmark
ksd output.Virtual-1.enable
sleep 5
note "readopt order: $(dlog "ScrollEngine::readoptParked: .*" | grep -o "$AU\|$CU" | sed "s/$AU/a/; s/$CU/c/" | tr '\n' ' ')"
echo "  a $(geo a) | b $(geo b) | c $(geo c)"
desk 2
sleep 1.5
ids
check "the strip on desktop 2" "$(striporder Virtual-1)" = "a | c"
check "b's output" "$(outof "$(geo b)")" = Virtual-0
finish
