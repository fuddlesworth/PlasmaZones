#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: GPL-3.0-or-later
#
# f648-stash-forgets-departed: a scrolling strip stashed by a mode switch forgets a window that
# left it, keeps one that is still there, and gives nothing to a newcomer of the same app (F648,
# F696, F714). Runner: 2 outputs, no flags.
# Staging: three desktops, Virtual-0 scrolling with s1 s2 s3 on desktop 1. Each part widens a
#   column (setColumnWidthProportion 0.8), switches Virtual-0 to snapping (COLS3), moves a window,
#   and switches Virtual-0 back to scrolling:
#     A  s2 to Virtual-1 and back;  B  s2 to desktop 2 and back;
#     C1 s2 put on all desktops;    C2 s2 put on desktops 1 and 3;
#     D  s1 to Virtual-1, then a new window n1 (same app id) opened on Virtual-0.
# Asserts: A and B bring s2 back at the default width with a forgetStashedWindowWhere line for
#   it; C1 and C2 keep s2's wide column with no forget line for desktop 1; in D n1 gets the
#   default width, not s1's wide column.
# A FAIL means: the stash put a departed window back in its old wide column, or handed that
#   column to another window of the same app.
set -u
NEST="$1"
LOG="$2"
. "$(dirname "$0")/../lib.sh"
kwjs_ok
need_outputs 2
newdesk 1 D2
sleep 0.5
newdesk 2 D3
setting snapAssistEnabled false
for c in s1 s2 s3; do
    open "$c"
    sendto "$c" Virtual-0 || stage_fail "$c is not on Virtual-0 ($(geo "$c"))"
done
assign Virtual-0 "scrolling:"
sleep 4
ids
snapmode() {
    assign Virtual-0 "$COLS3"
    sleep 3
}
scrollmode() {
    assign Virtual-0 "scrolling:"
    sleep 3.5
}
widen() {
    activate "$1"
    sleep 0.4
    PZ org.plasmazones.Scrolling.setColumnWidthProportion Virtual-0 0.8 >/dev/null
    sleep 1.5
}
setdesks() {
    on "$1" "w[i].desktops = [$2];"
    sleep 1.5
}
S2=$(uuid s2)
S1=$(uuid s1)
[ -n "$S2" ] && [ -n "$S1" ] || stage_fail "s1 or s2 never mapped"
echo "strip: $(stripx)"
DEF=$(extent s2)
[ -n "$DEF" ] || stage_fail "s2 is not in Virtual-0's strip ($(stripx))"

echo "== A: s2 to Virtual-1 and back"
widen s2
WIDE=$(extent s2)
echo "  widened: $(stripx)"
[ -n "$WIDE" ] && [ "$WIDE" -gt "$DEF" ] || stage_fail "s2's column did not widen ($(stripx))"
dmark
snapmode
sendto s2 Virtual-1 || stage_fail "s2 did not reach Virtual-1"
sendto s2 Virtual-0 || stage_fail "s2 did not come back to Virtual-0"
scrollmode
echo "  back: $(stripx)"
check "A: s2's column width after it left for Virtual-1 and came back" "$(extent s2)" = "$DEF"
check "A: forget lines for s2" "$(dlog "forgetStashedWindowWhere: .*$S2" | wc -l)" -ge 1

echo "== B: s2 to desktop 2 and back"
widen s2
dmark
snapmode
setdesks s2 "workspace.desktops[1]"
setdesks s2 "workspace.desktops[0]"
scrollmode
echo "  back: $(stripx)"
check "B: s2's column width after it went to desktop 2 and back" "$(extent s2)" = "$DEF"
check "B: forget lines for s2" "$(dlog "forgetStashedWindowWhere: .*$S2" | wc -l)" -ge 1

echo "== C1: s2 on all desktops"
widen s2
dmark
snapmode
on s2 'w[i].onAllDesktops = true;'
sleep 1.5
scrollmode
echo "  back: $(stripx)"
check "C1: s2's column width while on all desktops" "$(extent s2)" = "$WIDE"
check "C1: forget lines for s2" "$(dlog "forgetStashedWindowWhere: .*$S2" | wc -l)" -eq 0
on s2 'w[i].onAllDesktops = false; w[i].desktops = [workspace.desktops[0]];'
sleep 2

echo "== C2: s2 on desktops 1 and 3"
widen s2
dmark
snapmode
setdesks s2 "workspace.desktops[0], workspace.desktops[2]"
scrollmode
echo "  back: $(stripx)"
check "C2: s2's column width on desktop 1" "$(extent s2)" = "$WIDE"
check "C2: forget lines naming desktop 1 for s2" "$(dlog "forgetStashedWindowWhere: .*$S2" | grep -c "desktop 1 ")" -eq 0
setdesks s2 "workspace.desktops[0]"

echo "== D: s1 leaves, n1 opens"
widen s1
echo "  widened s1: $(stripx)"
snapmode
sendto s1 Virtual-1 || stage_fail "s1 did not reach Virtual-1"
open n1
sendto n1 Virtual-0 || stage_fail "n1 is not on Virtual-0 ($(geo n1))"
ids
scrollmode
echo "  back: $(stripx)"
N1=$(extent n1)
[ -n "$N1" ] || stage_fail "n1 is not in Virtual-0's strip ($(stripx))"
check "D: n1's column width" "$N1" = "$DEF"
check "D: s1 in Virtual-0's strip after it left" "$(extent s1)" = ""
finish
