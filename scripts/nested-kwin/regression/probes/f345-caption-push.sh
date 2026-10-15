#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: GPL-3.0-or-later
#
# f345-caption-push: the effect's caption push records what it sent (F345, F170), and a title
# rule follows the title (F864). Runner: 1 output, no flags.
# Staging: snapping Virtual-0, Snap Assist off. The daemon's view of a caption is
#   WindowTracking.getWindowMetadata. F345: g1 (retitle_gtk.py) opens as e15t0 and retitles to
#   e15T1 with the daemon up; the daemon is stopped, g1 retitles to e15T2, the daemon starts again
#   (the bring-up pushes e15T2), then g1 retitles back to e15T1. F170: g2 opens as e15f170 and its
#   first caption change is to "". F864: a rule Title contains "red" -> setHideTitleBar; q1
#   (retitle_qt.py, server-side frame) is focused and left still, then retitles to e15red and back
#   to e15blue with no focus change.
# Asserts: the registry ends on e15T1 after the change back; g2's registry title is "" once
#   KWin's caption is ""; q1's noBorder is true right after the retitle to e15red, still true 1 s
#   later and after a focus round trip, and false right after the retitle back to e15blue.
# A FAIL means: the de-dupe swallowed a change back made while the daemon was down, a first change
#   to "" was never pushed, or a title rule waited for a focus change.
set -u
NEST="$1"
LOG="$2"
. "$(dirname "$0")/../lib.sh"
kwjs_ok
# kw UUID -> "cap=[CAPTION] noBorder=B" for the window with internalId {UUID}
kw() {
    local nonce="$RANDOM$RANDOM"
    js "const w = workspace.windowList(); for (var i = 0; i < w.length; i++) { if ((\"\" + w[i].internalId) === \"{$1}\") { print(\"KW $nonce cap=[\" + w[i].caption + \"] noBorder=\" + w[i].noBorder); } }"
    grep "js: KW $nonce " "$LOG" | tail -1 | sed "s/.*js: KW $nonce //"
}
kwnb() { kw "$1" | grep -o "noBorder=[a-z]*" | cut -d= -f2; }
mdtitle() { qdbus6 org.plasmazones /PlasmaZones org.plasmazones.WindowTracking.getWindowMetadata "$1" 2>&1 | sed -n 2p; }
activateid() {
    js "const w = workspace.windowList(); for (var i = 0; i < w.length; i++) { if ((\"\" + w[i].internalId) === \"{$1}\") { workspace.activeWindow = w[i]; } }"
}
retitle() { printf '%s\n' "$2" > "$1"; }
setting snapAssistEnabled false

echo "== F345: a title changed while the daemon was down, then changed back"
CTL1="$NEST/e15-g1.ctl"
client retitle_gtk.py e15t0 "$CTL1" 400 300
ids
G1U=$(uuid e15t0)
[ -n "$G1U" ] || stage_fail "g1 (e15t0) never mapped"
G1="org.pz.plain|$G1U"
retitle "$CTL1" e15T1
sleep 1.5
echo "g1 retitled to e15T1 with the daemon up: KWin $(kw "$G1U") | registry title $(mdtitle "$G1")"
[ "$(mdtitle "$G1")" = e15T1 ] || stage_fail "the registry did not take e15T1 with the daemon up"
dkill
echo "nested daemon stopped"
sleep 1
retitle "$CTL1" e15T2
sleep 1.5
echo "g1 retitled to e15T2 while down: KWin $(kw "$G1U")"
emark
drestart
for _ in $(seq 40); do
    elog "Bridge registered" | grep -q . && break
    sleep 0.5
done
elog "Bridge registered" | grep -q . || stage_fail "the effect never re-registered with the restarted daemon"
sleep 3
[ "$(mdtitle "$G1")" = e15T2 ] || stage_fail "the bring-up did not push e15T2 ($(mdtitle "$G1"))"
retitle "$CTL1" e15T1
sleep 1.5
echo "g1 retitled back to e15T1: KWin $(kw "$G1U")"
check "F345 registry title after the change back" "$(mdtitle "$G1")" = e15T1

echo "== F170: a first caption change to the empty string"
CTL2="$NEST/e15-g2.ctl"
client retitle_gtk.py e15f170 "$CTL2" 400 300
ids
G2U=$(uuid e15f170)
[ -n "$G2U" ] || stage_fail "g2 (e15f170) never mapped"
G2="org.pz.plain|$G2U"
[ "$(mdtitle "$G2")" = e15f170 ] || stage_fail "the registry did not take g2's first title ($(mdtitle "$G2"))"
retitle "$CTL2" ""
sleep 1.5
K2=$(kw "$G2U")
echo "g2 retitled to \"\": KWin $K2"
case "$K2" in "cap=[] "*) ;; *) stage_fail "KWin's caption for g2 is not empty ($K2)" ;; esac
check "F170 registry title after the change to \"\"" "[$(mdtitle "$G2")]" = "[]"

echo "== F864: a Title rule follows the title"
RID="{11111111-2222-4333-8444-5555555e1501}"
RULE='{"actions":[{"value":true,"type":"setHideTitleBar"}],"enabled":true,"id":"'"$RID"'","match":{"all":[{"field":"title","op":"contains","value":"red"}]},"name":"probe F864 title","priority":50}'
[ "$(PZ org.plasmazones.Rules.addRule "$RULE")" = true ] || stage_fail "Rules.addRule refused the title rule"
on_exit "PZ org.plasmazones.Rules.removeRule '$RID' >/dev/null"
sleep 1.5
CTL3="$NEST/e15-q1.ctl"
qtclient retitle_qt.py e15blue "$CTL3" 500 400
sleep 1
ids
Q1U=$(uuid e15blue)
[ -n "$Q1U" ] || stage_fail "q1 (e15blue) never mapped (PySide6 client)"
activateid "$Q1U"
sleep 1
echo "q1 focused and still: $(kw "$Q1U")"
[ "$(kwnb "$Q1U")" = false ] || stage_fail "q1 has no server-side title bar to hide ($(kw "$Q1U"))"
retitle "$CTL3" e15red
sleep 0.6
check "F864 q1 noBorder right after the retitle to e15red" "$(kwnb "$Q1U")" = true
sleep 1
check "F864 q1 noBorder 1 s after the retitle to e15red" "$(kwnb "$Q1U")" = true
activateid "$G1U"
sleep 0.6
activateid "$Q1U"
sleep 1
check "F864 q1 noBorder after a focus round trip" "$(kwnb "$Q1U")" = true
retitle "$CTL3" e15blue
sleep 0.6
check "F864 q1 noBorder right after the retitle back to e15blue" "$(kwnb "$Q1U")" = false
finish
