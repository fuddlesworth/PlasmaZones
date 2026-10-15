#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: GPL-3.0-or-later
#
# f71-strip-counter-own-move: the strip's counter-assert acts only on its own command, so an X11
# window moved out of the strip is not pulled back by a nudge that lands before the release.
# Runner: 2 outputs, flags XS.
# Staging: Virtual-0 scrolling, Virtual-1 COLS3, Snap Assist off, x1 an X11 client alone in the
#   strip. Per attempt a KWin script stays loaded, waits for x1's output to turn Virtual-1 after
#   move_window_right (the cross-mode move into COLS3 zone 1), and DELAY ms later nudges x1's
#   frame one pixel right, outside any apply. Between attempts move_window_left returns x1 to the
#   strip. Five attempts, DELAY 0, 15, 40, 80 and 150 ms.
# Asserts: on every attempt that ran, no "Countering external move of scroll-managed window" line
#   for x1, and x1 stays on Virtual-1.
# A FAIL means: a nudge that landed before the strip's release was countered and x1 jumped back to
#   its Virtual-0 column.
set -u
NEST="$1"
LOG="$2"
. "$(dirname "$0")/../lib.sh"
kwjs_ok
need_outputs 2
setting snapAssistEnabled false
assign Virtual-0 "scrolling:"
assign Virtual-1 "$COLS3"
sleep 2
xclient plain_gtk.py x1 500 400
sendto x1 Virtual-0 || stage_fail "x1 is not on Virtual-0 ($(geo x1))"
sleep 0.5
ids
X1=$(uuid x1)
[ -n "$X1" ] || stage_fail "x1 never mapped"
echo "x1 in the strip: $(geo x1)"
ARM="$NEST/f71-arm.js"
on_exit "rm -f '$ARM'"
attempt=0
ran=0
for DELAY in 0 15 40 80 150; do
    attempt=$((attempt + 1))
    [ "$(outof "$(geo x1)")" = Virtual-0 ] || {
        note "attempt $attempt: x1 is not back in the strip ($(geo x1)); this attempt does not count"
        continue
    }
    activate x1 0.8
    emark
    printf '%s\n' "var t0 = Date.now(); var done = false; var x = null; const w = workspace.windowList();
for (var i = 0; i < w.length; i++) { if (w[i].caption === \"x1\") { x = w[i]; } }
if (x) {
    x.outputChanged.connect(function () {
        if (done || x.output.name !== \"Virtual-1\") { return; }
        done = true;
        print(\"F71 out \" + (Date.now() - t0) + \"ms at \" + x.frameGeometry.x + \",\" + x.frameGeometry.y);
        var t = new QTimer();
        t.singleShot = true;
        t.interval = $DELAY;
        t.timeout.connect(function () {
            var g = x.frameGeometry;
            x.frameGeometry = {x: g.x + 1, y: g.y, width: g.width, height: g.height};
            print(\"F71 nudge \" + (Date.now() - t0) + \"ms to \" + (g.x + 1) + \",\" + g.y);
        });
        t.start();
    });
}" > "$ARM"
    qdbus6 org.kde.KWin /Scripting org.kde.kwin.Scripting.loadScript "$ARM" >/dev/null
    qdbus6 org.kde.KWin /Scripting org.kde.kwin.Scripting.start >/dev/null 2>&1
    sleep 0.3
    shortcut move_window_right
    sleep 2
    qdbus6 org.kde.KWin /Scripting org.kde.kwin.Scripting.unloadScript "$ARM" >/dev/null 2>&1
    OUT=$(elog "F71 out .*" | head -1)
    NUDGE=$(elog "F71 nudge .*" | head -1)
    echo "attempt $attempt delay ${DELAY}ms: [$OUT] [$NUDGE] x1 now $(geo x1)"
    if [ -z "$OUT" ] || [ -z "$NUDGE" ]; then
        note "attempt $attempt: x1 never reached Virtual-1 or was never nudged; this attempt does not count"
    else
        ran=$((ran + 1))
        check "attempt $attempt (delay ${DELAY} ms): counter lines for x1" \
            "$(elog "Countering external move of scroll-managed window .*$X1" | wc -l)" -eq 0
        check "attempt $attempt (delay ${DELAY} ms): x1's output after the nudge" "$(outof "$(geo x1)")" = Virtual-1
    fi
    activate x1 0.8
    if [ "$(outof "$(geo x1)")" = Virtual-1 ]; then
        shortcut move_window_left
        sleep 2.5
    fi
    echo "  back in the strip for the next attempt: $(geo x1)"
done
[ "$ran" -gt 0 ] || stage_fail "no attempt moved and nudged x1"
finish
