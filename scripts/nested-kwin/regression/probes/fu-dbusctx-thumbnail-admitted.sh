#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: GPL-3.0-or-later
#
# fu-dbusctx-thumbnail-admitted: the effect's snap-assist thumbnail push is admitted by the
# compositor trust gate, with kwin_wayland as the session's own compositor (no wrapper).
# Runner: 1 output, flag F, PLASMAZONES_THUMBNAIL_TRACE=1 in the compositor's environment (kenv).
#   The -pixels row adds PLASMAZONES_DMABUF_THUMBNAILS=0, so the pushes take the raw-pixel
#   setSnapAssistThumbnail path instead of the dma-buf one.
# Staging: Snap Assist on, Virtual-0 COLS3, three windows a, b, c. a is Alt-dragged into zone 1,
#   so Snap Assist opens with b and c as candidates and the effect captures and pushes their
#   thumbnails, logging one "result=" trace line per push.
# Asserts: the daemon logs "compositor: pid <kwin> trusts its children false" and "trustIfCompositor:
#   admitted <name> pid= <kwin> compositor= <kwin>" (the equality arm), with <kwin> the nested
#   kwin_wayland's pid, and no "trustIfCompositor: rejecting"; Snap Assist opens; at least one push
#   logs result=accepted, none result=error, and no "authenticateKwinSender: refused" line.
# On main (8ca183308): the gate is validateExeAndTrust, which logs "validateExeAndTrust: cannot
#   resolve /proc/<kwin>/exe — rejecting" where kwin_wayland carries a file capability, and no
#   "trustIfCompositor:" lines exist.
set -u
NEST="$1"
LOG="$2"
. "$(dirname "$0")/../lib.sh"
kwjs_ok
KP=$(kwinpid)
[ -n "$KP" ] || stage_fail "no nested kwin_wayland pid for socket $WAYLAND_DISPLAY"
grep -q "PLASMAZONES_THUMBNAIL_TRACE set" "$LOG" || note "the effect has not announced the thumbnail trace yet (it does at the first capture)"
dlog "compositor: pid.*|trustIfCompositor: .*|validateExeAndTrust: .*" | head -6 | sed 's/^/    /'
check "the compositor read (nested kwin $KP, no wrapper)" \
    "$(dlog "compositor: pid $KP trusts its children false" | wc -l)" -ge 1
check "trustIfCompositor admitted the nested kwin" \
    "$(dlog "trustIfCompositor: admitted [^ ]+ pid= $KP compositor= $KP" | wc -l)" -ge 1
check "trustIfCompositor refusals" "$(dlog "trustIfCompositor: rejecting" | wc -l)" -eq 0
setting snapAssistEnabled true
assign Virtual-0 "$COLS3"
sleep 1.5
for c in a b c; do
    open "$c" 300 300
done
read -r _ _ SW SH <<< "$(scr Virtual-0)"
emark
dmark
altdrag a $((SW / 6)) $((SH / 2))
sleep 4
echo "a after the drop: $(geo a)"
elog "Snap Assist (shown|skipped).*" | sed 's/^/    /'
[ "$(elog "Snap Assist shown with" | wc -l)" -ge 1 ] || stage_fail "Snap Assist did not open after the drop"
elog "result=[a-z]+" | sed 's/^/    /'
check "thumbnail pushes with result=accepted" "$(elog "result=accepted" | wc -l)" -ge 1
check "thumbnail pushes with result=error" "$(elog "result=error" | wc -l)" -eq 0
check "authenticateKwinSender refusals" "$(dlog "authenticateKwinSender: refused" | wc -l)" -eq 0
finish
