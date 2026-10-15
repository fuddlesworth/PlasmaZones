#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: GPL-3.0-or-later
#
# f991-late-tileable-adopt: a window that becomes tileable late (keep-above cleared) is tiled at
# once, without taking focus. Runner: 1 output, no flags.
# Staging: Virtual-0 autotile:bsp, t1 then t2 (plain_gtk.py), two tiles. Run 1: t1 kept above (it
#   is evicted and floats), then cleared. Run 2: a KWin script left loaded keeps any window of app
#   org.pz.late above as it is added; t3 (appid_gtk.py, org.pz.late) opens, the script is unloaded,
#   then t3's keep-above is cleared. Run 3: t1 kept above, minimized, cleared while minimized,
#   then unminimized.
# Asserts: run 1: t1 is evicted, then one "Notified autotile: windowReannounced" for t1 after the
#   clear, two tiles again and the active window unchanged. Run 2: no announce for t3 while kept
#   above, then one windowReannounced, t3 listed among the tiles and the active window unchanged.
#   Run 3: no announce and no adoption for t1 while it is minimized.
# A FAIL means: a window cleared of keep-above was left floating until something else retiled.
set -u
NEST="$1"
LOG="$2"
. "$(dirname "$0")/../lib.sh"
kwjs_ok
ntiles() { tiles Virtual-0 | awk '{ print $1 }'; }
reannounced() { elog "Notified autotile: windowReannounced [^ ]*$1" | wc -l; }
announced() { elog "Notified autotile: window(Opened|Reannounced) [^ ]*$1" | wc -l; }
adoptions() { elog "Flags-settle adoption: [^ ]*$1" | wc -l; }
assign Virtual-0 "autotile:bsp"
sleep 2
open t1 400 300
open t2 400 300
ids
T1=$(uuid t1)
T2=$(uuid t2)
[ -n "$T1" ] && [ -n "$T2" ] || stage_fail "t1 or t2 never mapped"
echo "staged: t1 $(geo t1) | t2 $(geo t2) | tiles $(tiles Virtual-0)"
[ "$(ntiles)" = 2 ] || stage_fail "not two tiles ($(tiles Virtual-0))"

echo "== run 1: t1 kept above, then cleared"
emark
on t1 "w[i].keepAbove = true;"
sleep 1.5
echo "  kept above: t1 $(geo t1) | tiles $(tiles Virtual-0)"
[ "$(elog "Flags-settle eviction: [^ ]*$T1" | wc -l)" -ge 1 ] || stage_fail "keep-above did not evict t1"
ACT0=$(activecap)
emark
on t1 "w[i].keepAbove = false;"
sleep 1.5
echo "  cleared: t1 $(geo t1) | t2 $(geo t2) | tiles $(tiles Virtual-0)"
check "run 1: tiles after the clear" "$(ntiles)" -eq 2
check "run 1: windowReannounced lines for t1 after the clear" "$(reannounced "$T1")" -eq 1
check "run 1: the active window across the clear" "$(activecap)" = "$ACT0"

echo "== run 2: t3 opens kept above, then cleared"
JS="$NEST/f991-late.js"
printf '%s\n' 'workspace.windowAdded.connect(function (w) { if (w.resourceClass === "org.pz.late" || w.caption === "t3") { w.keepAbove = true; print("F991LATE kept " + w.caption + " above at add"); } });' > "$JS"
qdbus6 org.kde.KWin /Scripting org.kde.kwin.Scripting.loadScript "$JS" >/dev/null
qdbus6 org.kde.KWin /Scripting org.kde.kwin.Scripting.start >/dev/null 2>&1
on_exit "qdbus6 org.kde.KWin /Scripting org.kde.kwin.Scripting.unloadScript '$JS' >/dev/null 2>&1; rm -f '$JS'"
sleep 0.7
emark
openapp t3 org.pz.late 400 300
qdbus6 org.kde.KWin /Scripting org.kde.kwin.Scripting.unloadScript "$JS" >/dev/null 2>&1
ids
T3=$(uuid t3)
[ -n "$T3" ] || stage_fail "t3 never mapped"
echo "  t3 at open: $(geo t3) | tiles $(tiles Virtual-0)"
[ "$(wprop t3 'w.keepAbove')" = true ] || stage_fail "t3 was not kept above at open"
check "run 2: announces for t3 while kept above" "$(announced "$T3")" -eq 0
ACT0=$(activecap)
TB=$(ntiles)
emark
on t3 "w[i].keepAbove = false;"
sleep 1.5
echo "  cleared: t3 $(geo t3) | tiles $(tiles Virtual-0)"
check "run 2: tiles after the clear" "$(ntiles)" -eq $((TB + 1))
check "run 2: t3 listed among the tiles" "$(tiles Virtual-0)" '~' "'${T3: -8}'"
check "run 2: windowReannounced lines for t3 after the clear" "$(reannounced "$T3")" -eq 1
check "run 2: the active window across the clear" "$(activecap)" = "$ACT0"

echo "== run 3: cleared while minimized"
on t1 "w[i].keepAbove = true;"
sleep 1.5
on t1 "w[i].minimized = true;"
sleep 1
[ "$(wprop t1 'w.minimized')" = true ] || stage_fail "t1 did not minimize"
echo "  t1 kept above and minimized: $(geo t1) | tiles $(tiles Virtual-0)"
emark
on t1 "w[i].keepAbove = false;"
sleep 1.5
check "run 3: announces for t1 while minimized" "$(announced "$T1")" -eq 0
check "run 3: adoption lines for t1 while minimized" "$(adoptions "$T1")" -eq 0
on t1 "w[i].minimized = false;"
sleep 1.5
echo "  unminimized: t1 $(geo t1) | tiles $(tiles Virtual-0)"
finish
