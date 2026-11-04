#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: GPL-3.0-or-later
#
# f1182-user-maximize-borderless: with KWin's borderless maximized windows on, a tile the user
# maximizes stays maximized (discussion #1182). Runner: 1 output, no flags.
# Staging: BorderlessMaximizedWindows=true in the nested kwinrc and a KWin reconfigure, Virtual-0
#   autotile:dwindle, two server-decorated Qt windows a and b (minsize_qt.py) declaring a 300x250
#   minimum. Staging gate: KWin reads the option back, both are tiled, b has a title bar.
#   Then a user setMaximize(true, true) on b, a user restore, and Virtual-0 switched to
#   autotile:monocle as the control.
# Asserts: b stays maximized and borderless and the effect reports no minimum for it; the restore
#   puts it back at its tile with its title bar; under monocle b is maximized by the tiling and its
#   minimum without the title bar is reported.
# A FAIL means: the decoration KWin took off for the user's maximize was reported as b's minimum,
#   the daemon re-tiled on it, and that batch cleared the maximize.
set -u
NEST="$1"
LOG="$2"
. "$(dirname "$0")/../lib.sh"
kwjs_ok
on_exit 'kwriteconfig6 --file "$XDG_CONFIG_HOME/kwinrc" --group Windows --key BorderlessMaximizedWindows --delete; qdbus6 org.kde.KWin /KWin reconfigure >/dev/null 2>&1'
kwriteconfig6 --file "$XDG_CONFIG_HOME/kwinrc" --group Windows --key BorderlessMaximizedWindows true
qdbus6 org.kde.KWin /KWin reconfigure >/dev/null
sleep 1
[ "$(jsread BMW 'options.borderlessMaximizedWindows')" = true ] \
    || stage_fail "KWin did not take BorderlessMaximizedWindows from the nested kwinrc"
assign Virtual-0 "autotile:dwindle"
sleep 2
for c in a b; do
    qtclient minsize_qt.py "$c" 500 400 300 250
done
sleep 2
ids
B=$(uuid b)
[ -n "$B" ] || stage_fail "b never mapped"
T=$(geo b)
echo "a $(geo a) | b $T | tiles $(tiles Virtual-0)"
[ "$(tiles Virtual-0 | cut -d' ' -f1)" = 2 ] || stage_fail "a and b are not both tiled on Virtual-0"
[ "$(nbof "$T")" = false ] || stage_fail "b has no server-side title bar ($T)"

echo "== user maximize of b"
dmark
on b 'w[i].setMaximize(true, true);'
sleep 3
M=$(geo b)
echo "  b $M"
dlog "windowMinSizeUpdated: [^ ]*$B.*|recalculateLayout: .*" | sed 's/^/    /'
check "b's maximize mode after the user maximize" "$(maxof "$M")" = 3
check "b borderless while maximized" "$(nbof "$M")" = true
check "minimum reports for b after the user maximize" "$(dlog "windowMinSizeUpdated: [^ ]*$B" | wc -l)" -eq 0

echo "== user restore of b"
dmark
on b 'w[i].setMaximize(false, false);'
sleep 3
R=$(geo b)
echo "  b $R | tiles $(tiles Virtual-0)"
check "b back at its tile after the restore" "$(rect "$R")" = "$(rect "$T")"
check "b's title bar back after the restore" "$(nbof "$R")" = false
check "tiled windows on Virtual-0 after the restore" "$(tiles Virtual-0 | cut -d' ' -f1)" -eq 2

echo "== control: Virtual-0 switched to monocle"
dmark
assign Virtual-0 "autotile:monocle"
sleep 3
G=$(geo b)
echo "  a $(geo a) | b $G"
dlog "windowMinSizeUpdated: [^ ]*$B.*" | sed 's/^/    /'
check "b's maximize mode under monocle" "$(maxof "$G")" = 3
check "b borderless under monocle" "$(nbof "$G")" = true
check "minimum reports for b under monocle" "$(dlog "windowMinSizeUpdated: [^ ]*$B" | wc -l)" -ge 1
finish
