#!/bin/bash
# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Print every normal window's committed geometry and KWin output assignment
# from inside the nested compositor, via a throwaway KWin script. Committed
# geometry is the ground truth for boundary bugs; screenshots are not.
set -eu
NEST="${PZ_NESTED_DIR:-/tmp/pz-nested-$USER}"
. "$NEST/env.sh"
JS="$NEST/dump-$$.js"
cat > "$JS" <<'DUMPJS'
const wins = workspace.windowList();
for (var i = 0; i < wins.length; i++) {
    var w = wins[i];
    if (!w.normalWindow) continue;
    print("WIN " + w.resourceClass + " geo=" + w.frameGeometry.x + "," + w.frameGeometry.y
          + " " + w.frameGeometry.width + "x" + w.frameGeometry.height
          + " output=" + (w.output ? w.output.name : "?"));
}
DUMPJS
qdbus6 org.kde.KWin /Scripting org.kde.kwin.Scripting.loadScript "$JS" >/dev/null
qdbus6 org.kde.KWin /Scripting org.kde.kwin.Scripting.start >/dev/null 2>&1
sleep 0.7
journalctl --user -t kwin_wayland --since "4 seconds ago" | grep "WIN " | sed 's/.*WIN /WIN /'
rm -f "$JS"
