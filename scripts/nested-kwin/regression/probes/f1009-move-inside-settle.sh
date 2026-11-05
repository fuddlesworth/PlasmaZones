#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: GPL-3.0-or-later
#
# f1009-move-inside-settle: a Move to Screen made inside an output change's settle debounce is the
# user's move. Runner: 3 outputs, no flags.
# Staging: Virtual-0 and Virtual-1 both COLS3, Snap Assist off, s1 (plain_gtk.py) snapped into
#   zone 1 of Virtual-0. Virtual-2 holds nothing and is the output that changes. A KWin script that
#   sends s1 to Virtual-1 (workspace.sendClientToScreen: no interactive move, no move count) is
#   written ahead; kscreen-doctor disables Virtual-2 and the script starts at once. It counts only
#   when the compositor log shows the script's send ahead of "Applying debounced screen geometry
#   change".
# Asserts: s1's reportOutputSettle row has verdict 4 (UserMove), and s1 stays on Virtual-1 rather
#   than back at its Virtual-0 zone-1 rect.
# A FAIL means: the settle read the send as KWin's own and put s1 back (verdict 3, Reassert).
set -u
NEST="$1"
LOG="$2"
. "$(dirname "$0")/../lib.sh"
kwjs_ok
need_outputs 3
case "$SCR" in *"Virtual-2="*) ;; *) stage_fail "no Virtual-2 (run with 3 outputs)" ;; esac
on_exit 'ksd output.Virtual-2.enable >/dev/null'
setting snapAssistEnabled false
for o in Virtual-0 Virtual-1; do
    assign "$o" "$COLS3"
done
sleep 2
open s1 400 300
[ "$(outof "$(geo s1)")" = Virtual-0 ] || sendto s1 Virtual-0 || stage_fail "s1 not on Virtual-0 ($(geo s1))"
ids
S1U=$(uuid s1)
[ -n "$S1U" ] || stage_fail "s1 never mapped"
S1="org.pz.plain|$S1U"
PZ org.plasmazones.Control.snapWindowToZone "$S1" 1 Virtual-0 >/dev/null
sleep 2.5
Z1=$(zoneof "$S1")
G1=$(geo s1)
echo "s1 staged: $G1 zone=$Z1"
[ "$(outof "$G1")" = Virtual-0 ] || stage_fail "s1 not on Virtual-0"
case "$Z1" in "{"*) ;; *) stage_fail "s1 did not snap ($Z1)" ;; esac

echo "== Virtual-2 disabled, s1 sent to Virtual-1 inside the debounce"
JS="$NEST/f1009-send.js"
printf '%s\n' 'var o = null;' \
    'for (var i = 0; i < workspace.screens.length; i++) { if (workspace.screens[i].name === "Virtual-1") { o = workspace.screens[i]; } }' \
    'const w = workspace.windowList();' \
    'for (var j = 0; j < w.length; j++) { if (w[j].caption === "s1" && o) { print("F1009SENT from " + w[j].output.name); workspace.sendClientToScreen(w[j], o); } }' > "$JS"
emark
dmark
WAYLAND_DISPLAY="$WAYLAND_DISPLAY" kscreen-doctor output.Virtual-2.disable > "$NEST/pzr-ksd.out" 2>&1
KX=$?
qdbus6 org.kde.KWin /Scripting org.kde.kwin.Scripting.loadScript "$JS" >/dev/null
qdbus6 org.kde.KWin /Scripting org.kde.kwin.Scripting.start >/dev/null 2>&1
echo "kscreen-doctor disable exit=$KX ($(head -c 160 "$NEST/pzr-ksd.out" | tr '\n' ' '))"
sleep 0.8
qdbus6 org.kde.KWin /Scripting org.kde.kwin.Scripting.unloadScript "$JS" >/dev/null 2>&1
rm -f "$JS"
sleep 2
[ "$KX" = 0 ] || stage_fail "kscreen-doctor could not disable Virtual-2"
ORDER=$(elog "js: F1009SENT|Applying debounced screen geometry change" | head -3 | tr '\n' '|')
echo "log order: $ORDER"
case "$ORDER" in
"js: F1009SENT"*"Applying debounced"*) ;;
*) stage_fail "the send missed the 500 ms debounce (log order: $ORDER)" ;;
esac
settlerows "$S1U"
check "s1's settle verdict" "$(verdictof "$S1U")" = 4
G=$(geo s1)
echo "s1 now: $G zone=$(zoneof "$S1")"
check "s1's output after the settle" "$(outof "$G")" = Virtual-1
check "s1 back at its Virtual-0 zone-1 rect" "$(yesno "[ '$G' = '$G1' ]")" = no
finish
