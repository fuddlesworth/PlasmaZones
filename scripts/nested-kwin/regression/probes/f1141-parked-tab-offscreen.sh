#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: GPL-3.0-or-later
#
# f1141-parked-tab-offscreen: a hidden tab stays parked wholly below the outputs across tab switches.
# Runner: 1 output, S (1600x900).
# Staging: the output scrolling (default template). t1 and t2 open; t1's column consumes t2 and
#   turns tabbed, so one tab is parked. scroll_cycle_tab then switches tabs three times. Every
#   switch deactivates the outgoing tab, whose client acks that configure while it is parked.
# Asserts: after each switch, and again after a settle, exactly one tab sits on the output and the
#   other's committed frame starts below it (top past y 900).
# A FAIL means: KWin 6.8 clamped the parked tab back on its acked configure (it commits with its
#   title bar showing above y 900) and the counter-assert did not put it back at the park.
set -u
NEST="$1"
LOG="$2"
. "$(dirname "$0")/../lib.sh"
kwjs_ok
assign Virtual-0 "scrolling:"
sleep 2
for c in t1 t2; do open "$c"; done
ids
[ -n "$(uuid t2)" ] || stage_fail "t2 never mapped"
activate t1
sleep 0.4
for s in scroll_consume_window scroll_toggle_column_tabbed; do
    shortcut "$s"
    sleep 1.5
done
echo "strip: $(striporder Virtual-0)"
# tops -> "T1TOP T2TOP": the committed frame's top edge of each tab
tops() { echo "$(frame t1 | cut -d, -f2 | cut -d' ' -f1) $(frame t2 | cut -d, -f2 | cut -d' ' -f1)"; }
# parked LABEL: one tab on the output, the other wholly below it
parked() {
    local t a b shown hidden
    t=$(tops)
    a=${t% *}
    b=${t#* }
    echo "$1: t1 $(frame t1) | t2 $(frame t2)"
    [ -n "$a" ] && [ -n "$b" ] || stage_fail "a tab has no frame $1"
    if [ "$a" -lt 900 ]; then shown=$a hidden=$b; else shown=$b hidden=$a; fi
    check "the shown tab sits on the output $1" "$([ "$shown" -lt 900 ] && echo yes || echo no)" = yes
    check "the hidden tab starts below the output $1" "$([ "$hidden" -gt 900 ] && echo yes || echo no)" = yes
}
parked "once tabbed"
for n in 1 2 3; do
    shortcut scroll_cycle_tab
    sleep 1.5
    parked "after switch $n"
done
sleep 3
parked "after a settle"
finish
