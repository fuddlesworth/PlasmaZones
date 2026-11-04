#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: GPL-3.0-or-later
#
# f1170-cheatsheet-focus-fsp: the shortcut cheatsheet takes keyboard focus even under KWin's
# Extreme focus-stealing prevention, when its show is what maps the overlay shell. Runner: 1 output.
# Staging: focus-stealing prevention set to Extreme (kwinrc FocusStealingPreventionLevel=4 and a
#   KWin reconfigure). w opens and is the active window. A gate checks no plasmazonesd window is
#   mapped, so the cheatsheet's show maps the shell itself.
# Asserts: after toggle_cheatsheet the active window is the plasmazonesd shell; after a second
#   toggle (dismiss) and the fade, no plasmazonesd window is mapped.
# On the PR #1170 tree before its audit fix: the Exclusive keyboard request reached KWin with the
#   remapped surface's first buffer, so KWin decided activation in addWaylandWindow under
#   focus-stealing prevention, which at Extreme refuses it, and w stayed active.
set -u
NEST="$1"
LOG="$2"
. "$(dirname "$0")/../lib.sh"
kwjs_ok
need_outputs 1

shellcount() {
    jsread SHELLN '(function () { var n = 0; const w = workspace.windowList(); for (var i = 0; i < w.length; i++) { if (String(w[i].resourceClass).indexOf("plasmazonesd") >= 0) { n++; } } return n; })()'
}
activeclass() { jsread ACTC '(workspace.activeWindow ? String(workspace.activeWindow.resourceClass) : "(none)")'; }

kwriteconfig6 --file "$XDG_CONFIG_HOME/kwinrc" --group Windows --key FocusStealingPreventionLevel 4
qdbus6 org.kde.KWin /KWin reconfigure >/dev/null 2>&1
sleep 1
FSP=$(jsread FSPL 'options.focusStealingPreventionLevel')
[ "$FSP" = 4 ] || stage_fail "focus-stealing prevention did not read as Extreme ($FSP)"

open w 400 300
activate w
sleep 1
[ "$(activecap)" = w ] || stage_fail "w is not the active window ($(activecap))"
[ "$(shellcount)" = 0 ] || stage_fail "a plasmazonesd window is already mapped"

shortcut toggle_cheatsheet
sleep 1.5
[ "$(shellcount)" -ge 1 ] || stage_fail "the cheatsheet did not map the shell"
ACT=$(activeclass)
echo "active window class with the cheatsheet up: $ACT"
check "the cheatsheet's shell is the active window" "$(yesno "[ \"\${ACT#*plasmazonesd}\" != \"\$ACT\" ]")" = yes
shortcut toggle_cheatsheet
sleep 2
check "plasmazonesd windows mapped after the cheatsheet closed" "$(shellcount)" -eq 0
finish
