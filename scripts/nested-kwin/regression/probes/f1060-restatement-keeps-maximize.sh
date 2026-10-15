#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: GPL-3.0-or-later
#
# f1060-restatement-keeps-maximize: a free re-statement of a maximized window keeps the maximize.
# Runner: 1 output, no flags.
# Staging: Virtual-0 COLS3, Snap Assist off, snappingRestoreFloatedWindowsOnLogin true (the floated
#   position restore the relay needs). x (appid_gtk.py, app org.pz.relay) placed free at P =
#   (220,140), snapped into zone 1, floated back to P with toggle_window_float, then closed. A KWin
#   script stays loaded that maximizes any org.pz.relay window as it is added; x reopens, so the
#   float-restore relay (purpose Restatement) re-states P onto a maximized window. Staging gate: the
#   effect's slotApplyGeometryRequested for the reopened x lands after the hook's maximize.
# Asserts: x stays max=3 at the maximize area (not P's size); unmaximized it lands at P.
# A FAIL means: the apply moveResized the maximized window to P while KWin kept the maximize bit.
set -u
NEST="$1"
LOG="$2"
. "$(dirname "$0")/../lib.sh"
kwjs_ok
HOOK="$NEST/f1060-hook.js"
on_exit 'qdbus6 org.kde.KWin /Scripting org.kde.kwin.Scripting.unloadScript "$HOOK" >/dev/null 2>&1; rm -f "$HOOK"'
setting snapAssistEnabled false
setting snappingRestoreFloatedWindowsOnLogin true
assign Virtual-0 "$COLS3"
sleep 1.5
openapp x org.pz.relay 400 300
moveto x 220 140
sleep 1.5
P=$(geo x)
ids
XU=$(uuid x)
[ -n "$XU" ] || stage_fail "x never mapped"
X="org.pz.relay|$XU"
echo "x free at P: $P"
PZ org.plasmazones.Control.snapWindowToZone "$X" 1 Virtual-0 >/dev/null
sleep 2
echo "x snapped: $(geo x) zone=$(zoneof "$X")"
case "$(zoneof "$X")" in "{"*) ;; *) stage_fail "x did not snap" ;; esac
activate x
shortcut toggle_window_float
sleep 2
F=$(geo x)
echo "x floated: $F floating=$(floating "$X")"
[ "$(floating "$X")" = true ] || stage_fail "toggle_window_float did not float x"
[ "$(rect "$F")" = "$(rect "$P")" ] || note "the float did not land at P; P is taken as $(rect "$F")"
P="$F"
on x 'w[i].closeWindow();'
sleep 2
printf '%s\n' 'workspace.windowAdded.connect(function (w) { if (w.resourceClass === "org.pz.relay") { w.setMaximize(true, true); print("F1060MAX maximized " + w.caption + " at add"); } });' > "$HOOK"
qdbus6 org.kde.KWin /Scripting org.kde.kwin.Scripting.loadScript "$HOOK" >/dev/null
qdbus6 org.kde.KWin /Scripting org.kde.kwin.Scripting.start >/dev/null 2>&1
sleep 0.7
echo "== x reopened (maximized at add by the hook)"
MARK=$(($(wc -l < "$LOG") + 1))
openapp x org.pz.relay 400 300
sleep 1.5
qdbus6 org.kde.KWin /Scripting org.kde.kwin.Scripting.unloadScript "$HOOK" >/dev/null 2>&1
ids
XU2=$(uuid x)
[ -n "$XU2" ] || stage_fail "x did not reopen"
ORDER=$(tail -n +"$MARK" "$LOG" | grep "js: F1060MAX\|slotApplyGeometryRequested.*$XU2" | cut -c1-200)
echo "  order (hook print, then the effect's applies for x):"
echo "$ORDER" | sed 's/^/    /'
echo "$ORDER" | sed -n '/js: F1060MAX/,$p' | grep -q "slotApplyGeometryRequested" \
    || stage_fail "no relay apply for x after the hook's maximize, so the re-statement decides nothing"
A=$(geo x)
echo "  x $A"
check "x maximizeMode after the re-statement" "$(maxof "$A")" = 3
check "x size after the re-statement (not P's size)" "$(size "$A")" != "$(size "$P")"
on x 'w[i].setMaximize(false, false);'
sleep 2
check "x unmaximized at P" "$(rect "$(geo x)")" = "$(rect "$P")"
finish
