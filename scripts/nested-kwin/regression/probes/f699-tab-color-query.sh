#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: GPL-3.0-or-later
#
# f699-tab-color-query: a tab's rule colours follow its window's keep-above and maximize state.
# Runner: 1 output, no flags.
# Staging: one output scrolling (default template). t1 and t2 open; t1's column consumes t2 and
#   turns tabbed. Two rules on t2: keepAbove true -> tabColorActive #ff0000, isMaximized true ->
#   tabColorInactive #00ff00. Then t2 is set keep-above, back, maximized, back (KWin scripts), and
#   Tiling.scrollTabColors is queried for t2 after each edge.
# Asserts: the query answers an empty map at first, {activeColor #ff0000} with keep-above, an
#   empty map again, {inactiveColor #00ff00} while maximized, and an empty map after.
# A FAIL means: the query answered from a title-keyed memo, so it kept the colours of t2's first
#   query whatever the window's state.
set -u
NEST="$1"
LOG="$2"
. "$(dirname "$0")/../lib.sh"
kwjs_ok
assign Virtual-0 "scrolling:"
sleep 2
for c in t1 t2; do open "$c"; done
ids
T2U=$(uuid t2)
[ -n "$T2U" ] || stage_fail "t2 never mapped"
T2="org.pz.plain|$T2U"
activate t1
sleep 0.4
for s in scroll_consume_window scroll_toggle_column_tabbed; do
    shortcut "$s"
    sleep 1.5
done
echo "strip: $(striporder Virtual-0)"
for r in 'keepAbove|tabColorActive|#ff0000|a1' 'isMaximized|tabColorInactive|#00ff00|a2'; do
    IFS='|' read -r F A C N <<< "$r"
    R="{\"actions\":[{\"value\":\"$C\",\"type\":\"$A\"}],\"enabled\":true,\"id\":\"{11111111-2222-4333-8444-5555555555$N}\",\"match\":{\"all\":[{\"field\":\"title\",\"op\":\"equals\",\"value\":\"t2\"},{\"field\":\"$F\",\"op\":\"equals\",\"value\":true}]},\"name\":\"probe F699 $F\",\"priority\":50}"
    [ "$(PZ org.plasmazones.Rules.addRule "$R")" = true ] || stage_fail "the $F rule was not added"
done
sleep 2
query() {
    gdbus call --session --dest org.plasmazones --object-path /PlasmaZones \
        --method org.plasmazones.Tiling.scrollTabColors "$T2" 2>&1 | tr -d '\n'
}
# edge LABEL 'STATEMENT' STATE QUERY: run STATEMENT on t2; the staging holds when t2 then reads
# STATE ("keepAbove maximizeMode"), and the query must answer QUERY
edge() {
    local s
    on t2 "$2"
    sleep 1.5
    s=$(wprop t2 'w.keepAbove + " " + w.maximizeMode')
    echo "$1: t2 keepAbove maximizeMode = $s | query $(query)"
    [ "$s" = "$3" ] || stage_fail "t2 reads '$s' $1, not '$3'"
    check "scrollTabColors for t2 $1" "$(query)" = "$4"
}
[ "$(query)" = "(@a{sv} {},)" ] || stage_fail "t2's tab colours are not empty before the first edge ($(query))"
edge "with keep-above on" "w[i].keepAbove = true;" "true 0" "({'activeColor': <'#ff0000'>},)"
edge "with keep-above off" "w[i].keepAbove = false;" "false 0" "(@a{sv} {},)"
edge "while maximized" "w[i].setMaximize(true, true);" "false 3" "({'inactiveColor': <'#00ff00'>},)"
edge "after the unmaximize" "w[i].setMaximize(false, false);" "false 0" "(@a{sv} {},)"
finish
