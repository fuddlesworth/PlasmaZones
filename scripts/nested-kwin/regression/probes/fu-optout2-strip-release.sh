#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: GPL-3.0-or-later
#
# fu-optout2-strip-release: a scrolling desktop switched to tile with no algorithm (autotile:none)
# leaves every strip window on screen: a window with a pre-scroll free spot goes back to it, the
# rest come onto the screen at their size. Runner: 1 output, no flags.
# Staging: Snap Assist off, Virtual-0 COLS3. s1, s2, s3 open; s1 is moved to 100,100 and floated
#   with the float shortcut, so snapping records that spot as its free geometry. Virtual-0 is then
#   assigned scrolling and each column widened to 0.6 of the screen, so the strip runs past the
#   viewport (a staging gate: at least one window not fully on the screen). Desktop 1 is assigned
#   autotile:none.
# Asserts: s1 is back at its spot, with a "placeReleasedOnOptOut: <s1> ... (its pre-scroll free
#   geometry)" line; every window ends fully on the screen, none floating and none in a zone; a
#   window logged "(brought on screen)" keeps its strip size. s2 and s3 may also go back to a free
#   spot recorded at their open, so which of the two lines they get is printed, not asserted.
# On main (8ca183308): the drain held every restore an opt-out screen would get and dropped it,
#   so each window kept its strip rect (the off-viewport ones stayed off the screen) and no
#   placeReleasedOnOptOut line exists.
set -u
NEST="$1"
LOG="$2"
. "$(dirname "$0")/../lib.sh"
kwjs_ok
setting snapAssistEnabled false
assign Virtual-0 "$COLS3"
sleep 1.5
read -r SX SY SW SH <<< "$(scr Virtual-0)"
for c in s1 s2 s3; do
    open "$c" 400 300
done
ids
declare -A ID
for c in s1 s2 s3; do
    u=$(uuid "$c")
    [ -n "$u" ] || stage_fail "$c never mapped"
    ID[$c]="org.pz.plain|$u"
done
moveto s1 100 100
sleep 1
floatkey s1
SPOT=$(rect "$(geo s1)")
echo "s1's free spot in snapping: $SPOT ($(wstat s1 "${ID[s1]}"))"
[ "$(floating "${ID[s1]}")" = true ] || stage_fail "the float shortcut did not float s1 on COLS3"
case "$SPOT" in "100 100 "*) ;; *) stage_fail "s1 is not at 100,100 before the float ($SPOT)" ;; esac
echo "== Virtual-0 assigned scrolling"
assign Virtual-0 "scrolling:"
sleep 3
for c in s1 s2 s3; do
    activate "$c"
    PZ org.plasmazones.Scrolling.setColumnWidthProportion Virtual-0 0.6 >/dev/null
    sleep 1
done
sleep 1.5
echo "  strip: $(stripx)"
# onscreen CAPTION: "yes" when the window's frame lies wholly inside the output
onscreen() {
    local x y w h
    read -r x y w h _ <<< "$(geo "$1")"
    [ -n "$x" ] || { echo no; return; }
    if [ "${x%.*}" -ge "$SX" ] && [ "${y%.*}" -ge "$SY" ] && [ $((${x%.*} + ${w%.*})) -le $((SX + SW)) ] \
        && [ $((${y%.*} + ${h%.*})) -le $((SY + SH)) ]; then echo yes; else echo no; fi
}
declare -A OFF SIZE
offcount=0
for c in s1 s2 s3; do
    G=$(geo "$c")
    SIZE[$c]=$(size "$G")
    OFF[$c]=$(onscreen "$c")
    echo "  $c strip frame: $G on screen=${OFF[$c]}"
    [ "${OFF[$c]}" = no ] && offcount=$((offcount + 1))
    case "$(stripx)" in *"[$c "*) ;; *) stage_fail "$c is not a column of the strip" ;; esac
done
[ "$offcount" -ge 1 ] || stage_fail "no strip window runs past the viewport, so the landing is not exercised"
echo "== desktop 1 assigned autotile:none"
dmark
assignd Virtual-0 1 "autotile:none"
sleep 3
dlog "placeReleasedOnOptOut: .*" | sed 's/^/    /'
check "s1 back at its pre-scroll free spot" "$(rect "$(geo s1)")" = "$SPOT"
check "placeReleasedOnOptOut lines restoring s1's free spot" \
    "$(dlog "placeReleasedOnOptOut: [^ ]*${ID[s1]##*|}[^ ]* on the tiling opt-out.*\(its pre-scroll free geometry\)" | wc -l)" -eq 1
landed=0
for c in s1 s2 s3; do
    echo "  $c $(wstat "$c" "${ID[$c]}")"
    check "$c fully on the screen" "$(onscreen "$c")" = yes
    check "$c not floating" "$(floating "${ID[$c]}")" = false
    check "$c in no zone" "$(zoneof "${ID[$c]}")" = ""
    if [ "$c" != s1 ] && [ "${OFF[$c]}" = no ]; then
        # A free window the strip took in also has a pre-scroll spot recorded at its open, and
        # goes back there at its free size; only one brought on screen keeps its strip size.
        if [ "$(dlog "placeReleasedOnOptOut: [^ ]*${ID[$c]##*|}[^ ]* .*\(brought on screen\)" | wc -l)" -ge 1 ]; then
            check "$c kept its strip size coming on screen" "$(size "$(geo "$c")")" = "${SIZE[$c]}"
        fi
        [ "$(dlog "placeReleasedOnOptOut: [^ ]*${ID[$c]##*|}" | wc -l)" -ge 1 ] && landed=$((landed + 1))
    fi
done
note "placeReleasedOnOptOut lines for windows off the viewport other than s1: $landed"
check "placeReleasedOnOptOut lines in all" "$(dlog "placeReleasedOnOptOut: " | wc -l)" -ge 1
check "B's mode for desktop 1 (the opt-out still reads as tiling)" "$(modeof Virtual-0 1)" = 1
finish
