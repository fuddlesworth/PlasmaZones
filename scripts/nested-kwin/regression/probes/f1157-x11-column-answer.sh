#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: GPL-3.0-or-later
#
# f1157-x11-column-answer: an X11 strip window that keeps resizing itself wider than its column
# widens the column (#1157). Runner: 1 output, flags XS. PZ_F1157_SCALE (default 1) is the output
# scale set with kscreen-doctor before any window opens; the -scaled row runs it at 1.25 (F337).
# Staging: at a scale other than 1, Virtual-0 goes to that scale first (1600x900 device pixels,
#   1280x720 logical at 1.25) and the probe waits until KWin and the daemon both read it. Virtual-0
#   scrolling. x1 is insist_qt.py on Xwayland, following its configures, and n1 a plain GTK window
#   opened after it, so the strip is [x1 | n1]; n1 is set to a quarter of the strip with
#   setColumnWidthProportion. x1 is focused and its column extent E0 read. x1 is then told to hold
#   a client width whose frame is an eighth of the output wider than E0 (converted through x1's
#   frame and client widths, which differ under Xwayland scaling), height free. It resizes itself
#   with a ConfigureRequest KWin grants at once, and again 50 ms after anything resizes it back.
# Asserts: the counter-assert put the foreign width back at least once and at most twice (put back
#   once, then learned when the client insists with the same width); the effect reported a
#   committed size for x1; x1's column extent grew past E0 by at least half the overshoot and
#   matches x1's frame width; n1, when it is on the output, starts at or past x1's right edge.
# F337, at any scale: when the extent did not grow and the effect logged "Column answer ... not
#   settled" lines for x1 whose commanded rect differs from the frame by rounding alone, the probe
#   fails a check that says so. Both the learn (frameReadsAsColumnAnswer) and the report need the
#   frame to read settled, which compares KWin's moveResizeGeometry with the frame exactly, so an
#   X11 frame KWin rounds through device pixels at a fractional XWayland scale would never settle,
#   and the read and learn path would be inert there.
# The old build (main before #1157): X11 strip windows get no column offer, so nothing is reported
#   for x1, and the counter-assert puts the width back up to three times per rolling second, its
#   budget re-arming each second, so it fights the client for the whole hold and the client's
#   width stands over n1 between bursts. x1's extent stays at E0: the extent, report and put-back
#   count checks fail.
set -u
NEST="$1"
LOG="$2"
. "$(dirname "$0")/../lib.sh"
kwjs_ok
SCALE="${PZ_F1157_SCALE:-1}"
read -r OX OY OW OH <<< "$(scr Virtual-0)"
if [ "$SCALE" != 1 ]; then
    on_exit 'ksd output.Virtual-0.scale.1 >/dev/null'
    WANTW=$(awk -v w="$OW" -v s="$SCALE" 'BEGIN { printf "%d", w / s + 0.5 }')
    WANTH=$(awk -v h="$OH" -v s="$SCALE" 'BEGIN { printf "%d", h / s + 0.5 }')
    ksd "output.Virtual-0.scale.$SCALE"
    # The logical size and the daemon's scale follow the change a moment apart; wait for both.
    DSCALE=""
    for _ in $(seq 10); do
        sleep 1
        SCR=$(screens)
        read -r OX OY OW OH <<< "$(scr Virtual-0)"
        DSCALE=$(PZ org.plasmazones.Screen.getScreenInfo Virtual-0 | python3 -c 'import json,sys; print(json.load(sys.stdin).get("devicePixelRatio"))' 2>/dev/null)
        [ "$OW" = "$WANTW" ] && [ "$OH" = "$WANTH" ] && [ "$DSCALE" = "$SCALE" ] && break
    done
    echo "Virtual-0 $OX,$OY ${OW}x$OH, daemon scale $DSCALE"
    [ "$OW" = "$WANTW" ] && [ "$OH" = "$WANTH" ] || stage_fail "Virtual-0 is not ${WANTW}x$WANTH logical at scale $SCALE (${OW}x$OH)"
    [ "$DSCALE" = "$SCALE" ] || stage_fail "the daemon reads scale $DSCALE for Virtual-0, not $SCALE"
fi
assign Virtual-0 "scrolling:"
sleep 2
CTL="$NEST/f1157-x1.ctl"
xqtclient insist_qt.py x1 "$CTL" 0 0
open n1 400 300
sleep 1
ids
X1=$(uuid x1)
N1=$(uuid n1)
[ -n "$X1" ] || stage_fail "x1 never mapped"
[ -n "$N1" ] || stage_fail "n1 never mapped"
[ "$(striporder Virtual-0)" = "x1 | n1" ] || stage_fail "the strip is not [x1 | n1] ($(striporder Virtual-0))"
activate n1
PZ org.plasmazones.Scrolling.setColumnWidthProportion Virtual-0 0.25 >/dev/null
sleep 2
activate x1
sleep 2

# rd CAPTION: CAPTION's frame into RX RY RW RH, whole pixels
rd() {
    local g
    g=$(geo "$1")
    [ -n "$g" ] || stage_fail "no frame for $1"
    read -r RX RY RW RH _ <<< "$g"
    RX=${RX%.*}
    RY=${RY%.*}
    RW=${RW%.*}
    RH=${RH%.*}
}
E0=$(extent x1)
rd x1
FW0=$RW
[ -r "$CTL.size" ] || stage_fail "x1 wrote no client size to $CTL.size"
read -r CW0 _ < "$CTL.size"
echo "before: strip $(stripx Virtual-0) | x1 $(geo x1) client width $CW0 | n1 $(geo n1)"
[ -n "$E0" ] || stage_fail "x1 is not a column of the strip ($(stripx Virtual-0))"
[ "$(within "$FW0" "$E0" 4)" = yes ] || stage_fail "x1 did not take its column before the hold (frame $FW0, extent $E0)"
[ "$CW0" -gt 0 ] || stage_fail "x1's client width reads $CW0"
TW=$((E0 + OW / 8))
HOLD=$((CW0 + (TW - FW0) * CW0 / FW0))
echo "x1 holds client width $HOLD for a frame of about $TW (column $E0, output ${OW}x$OH)"

emark
dmark
printf '%s 0\n' "$HOLD" > "$CTL"
sleep 5
E1=$(extent x1)
rd x1
X1X=$RX
X1W=$RW
CW1=""
read -r CW1 _ < "$CTL.size"
COUNTERS=$(elog "Countering external move of scroll-managed window [^ ]*$X1" | wc -l)
REPORTS=$(elog "Reporting committed size for [^ ]*$X1" | wc -l)
echo "after: strip $(stripx Virtual-0) | x1 $(geo x1) client width $CW1 | n1 $(geo n1)"
echo "  put-backs of x1: $COUNTERS | committed-size reports for x1: $REPORTS"
elog "Countering external move of scroll-managed window [^ ]*$X1.*|Re-seating scroll-managed window [^ ]*$X1.*|Reporting committed size for [^ ]*$X1.*" \
    | head -8 | sed 's/^/    /'
dlog "reportCommittedSize: [^ ]*$X1.*" | head -4 | sed 's/^/    /'
[ "$COUNTERS" -gt 0 ] || [ "$REPORTS" -gt 0 ] || [ "$CW1" = "$HOLD" ] \
    || stage_fail "x1 never resized itself to $HOLD (client width $CW1, no put-back, no report)"

check "the first foreign width of x1 was put back" "$COUNTERS" -ge 1
check "x1 was learned on insisting rather than fought (put-backs)" "$COUNTERS" -le 2
check "committed-size reports for x1" "$REPORTS" -ge 1
GROWN=no
[ -n "$E1" ] && [ "$E1" -ge $((E0 + (TW - E0) / 2)) ] && GROWN=yes
check "x1's column extent grew from $E0 towards $TW (extent ${E1:-none})" "$GROWN" = yes
check "x1's column extent against its frame width $X1W" "$(within "${E1:-0}" "$X1W" 4)" = yes
rd n1
if [ "$RY" -lt $((OY + OH)) ] && [ "$RX" -lt $((OX + OW)) ]; then
    check "n1 starts at or past x1's right edge $((X1X + X1W))" "$RX" -ge $((X1X + X1W - 2))
else
    note "n1 is off the output ($(geo n1)), so the overlap check does not apply"
fi

# F337: the reads that came back unsettled only because KWin's move-resize geometry and the frame
# round apart. Counted over the whole run, not since the mark: x1's first answer, to the column it
# opened in, is read before the hold, and a frame that never settles leaves that answer owed (only
# an owed answer logs the line). Counted always, a check only when the column did not grow.
unsettled() { grep -oE "Column answer for [^ ]*$X1[^ ]* not settled.*" "$LOG" | cut -c1-220; }
ROUNDING=$(unsettled \
    | sed -n 's/.*commanded QRectF(\([-0-9.]*\),\([-0-9.]*\) \([0-9.]*\)x\([0-9.]*\)) frame QRect(\([-0-9]*\),\([-0-9]*\) \([0-9]*\)x\([0-9]*\)).*/\1 \2 \3 \4 \5 \6 \7 \8/p' \
    | awk 'NF == 8 { m = 0; for (i = 1; i <= 4; i++) { d = $i - $(i + 4); if (d < 0) d = -d; if (d > m) m = d } if (m < 1.5) n++ } END { print n + 0 }')
UNSETTLED=$(unsettled | wc -l)
echo "  unsettled column-answer reads for x1 in the whole run: $UNSETTLED, by rounding alone: $ROUNDING"
unsettled | tail -3 | sed 's/^/    /'
if [ "$GROWN" = no ]; then
    if [ "$ROUNDING" -gt 0 ]; then
        check "F337 at scale $SCALE: x1's frame never read settled, moveResizeGeometry and the frame differ by rounding alone ($ROUNDING reads), so the learn and the report never ran" \
            "$ROUNDING" -eq 0
    else
        note "the column did not grow and no read was unsettled by rounding alone, so F337 is not the cause here"
    fi
fi
finish
