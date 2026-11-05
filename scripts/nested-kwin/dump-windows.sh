#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Print every normal window's committed geometry and KWin output assignment
# from inside the nested compositor, via a throwaway KWin script. Committed
# geometry is the ground truth for boundary bugs; screenshots are not.
set -eu

# Distros ship the Qt6 D-Bus CLI under three names; daemon.sh resolves all
# three. Hardcoding qdbus6 made this script fail at loadScript under set -eu
# on a distro shipping only qdbus-qt6, with the cleanup trap's unloadScript
# then a silent no-op.
QDBUS=""
for _c in qdbus6 qdbus-qt6 qdbus; do
    if command -v "$_c" >/dev/null 2>&1; then QDBUS="$_c"; break; fi
done
if [ -z "$QDBUS" ]; then
    echo "no qdbus6/qdbus-qt6/qdbus on PATH; cannot drive the KWin scripting API" >&2
    exit 1
fi
# id -u, not $USER: this must resolve EXACTLY as run-nested.sh and
# daemon.sh do, including the fallback when XDG_RUNTIME_DIR is unset.
# daemon.sh carries a comment saying a divergence here points the script
# at a different state dir, and $USER additionally can be unset in a bare
# environment, which yielded /tmp/pz-nested-.
NEST="${PZ_NESTED_DIR:-${XDG_RUNTIME_DIR:-/tmp/pz-nested-$(id -u)}/pz-nested}"
if [ ! -r "$NEST/env.sh" ]; then
    echo "no nested session state at $NEST/env.sh; start run-nested.sh first" >&2
    exit 1
fi
. "$NEST/env.sh"
JS="$NEST/dump-$$.js"
SCRIPT_ID=""
cleanup() {
    if [ -n "$SCRIPT_ID" ]; then
        "$QDBUS" org.kde.KWin /Scripting org.kde.kwin.Scripting.unloadScript "$JS" >/dev/null 2>&1 || true
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
SCRIPT_ID=$("$QDBUS" org.kde.KWin /Scripting org.kde.kwin.Scripting.loadScript "$JS")
"$QDBUS" org.kde.KWin /Scripting org.kde.kwin.Scripting.start >/dev/null 2>&1
sleep 0.7
# No windows (or the journal lagging) is an answer, not an error — the
# grep's no-match exit must not abort the trap-guarded cleanup under set -e.
journalctl --user -t kwin_wayland --since "4 seconds ago" | grep "WIN " | sed 's/.*WIN /WIN /' || true
