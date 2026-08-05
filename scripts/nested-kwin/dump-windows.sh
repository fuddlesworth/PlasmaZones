#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Print every normal window's committed geometry and KWin output assignment
# from inside the nested compositor, via a throwaway KWin script. Committed
# geometry is the ground truth for boundary bugs; screenshots are not.
set -eu
NEST="${PZ_NESTED_DIR:-${XDG_RUNTIME_DIR:-/tmp/pz-nested-$USER}/pz-nested}"
if [ ! -r "$NEST/env.sh" ]; then
    echo "no nested session state at $NEST/env.sh; start run-nested.sh first" >&2
    exit 1
fi
. "$NEST/env.sh"
JS="$NEST/dump-$$.js"
SCRIPT_ID=""
cleanup() {
    if [ -n "$SCRIPT_ID" ]; then
        qdbus6 org.kde.KWin /Scripting org.kde.kwin.Scripting.unloadScript "$JS" >/dev/null 2>&1 || true
    fi
    rm -f "$JS"
}
trap cleanup EXIT INT TERM
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
SCRIPT_ID=$(qdbus6 org.kde.KWin /Scripting org.kde.kwin.Scripting.loadScript "$JS")
qdbus6 org.kde.KWin /Scripting org.kde.kwin.Scripting.start >/dev/null 2>&1
sleep 0.7
# No windows (or the journal lagging) is an answer, not an error — the
# grep's no-match exit must not abort the trap-guarded cleanup under set -e.
journalctl --user -t kwin_wayland --since "4 seconds ago" | grep "WIN " | sed 's/.*WIN /WIN /' || true
