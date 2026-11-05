#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: GPL-3.0-or-later
#
# f814-login-relative-spot: a floating spot saved relative to its output is restored at the same
# place on that output after a login with the outputs arranged differently. Runner: 2 outputs,
# flags SFK, after f814-spots-follow-output (it runs on the state home that probe leaves, where
# p1's spot was saved as 300,250 on Virtual-1 while Virtual-1 sat at 0,0).
# Staging: KWin may restore the previous session's swapped arrangement, so the daemon is stopped,
#   kscreen-doctor puts the outputs back (Virtual-0 at 0,0, Virtual-1 at W,0) and the daemon is
#   started again: it reads session.json with the outputs already arranged, the login shape. p1
#   (same app id) is reopened with the pointer on Virtual-1 (a reopen restores on the screen it
#   opens on), and the float shortcut takes it to its spot (snapped into zone 1 first when the
#   restore left it floating).
# Asserts: the start logs "Loaded session state: windowPlacements= 1"; the reopen restores p1
#   floating at Virtual-1's origin plus 300,250; the float shortcut puts it there too.
# A FAIL means: the spot was read as absolute and landed on Virtual-0, or was refused there.
set -u
NEST="$1"
LOG="$2"
. "$(dirname "$0")/../lib.sh"
SESSION="$XDG_CONFIG_HOME/plasmazones/session.json"
kwjs_ok
need_outputs 2
[ -f "$SESSION" ] || stage_fail "no session.json (f814-spots-follow-output must run first, and this one with flag K)"
P=$(placements)
echo "kept placements: $P"
case "$P" in *"Virtual-1: 300,250 "*) ;; *) stage_fail "the kept state holds no 300,250 spot on Virtual-1" ;; esac
echo "outputs at start: $SCR"
read -r _ _ V0W _ <<< "$(scr Virtual-0)"
dkill
ksd output.Virtual-0.position.0,0 output.Virtual-1.position.$V0W,0
sleep 3
SCR=$(screens)
echo "outputs after: $SCR"
[ "$(scr Virtual-0 | cut -d' ' -f1,2)" = "0 0" ] && [ "$(scr Virtual-1 | cut -d' ' -f1,2)" = "$V0W 0" ] \
    || stage_fail "kscreen-doctor did not restore the default arrangement ($SCR)"
read -r V1X V1Y V1W V1H <<< "$(scr Virtual-1)"
drestart
sleep 4
check "the start loads the session's placements" \
    "$(grep -c "Loaded session state: windowPlacements= 1" "$NEST/daemon.log")" -ge 1
WX=$((V1X + 300))
WY=$((V1Y + 250))
fake abs $((V1X + V1W / 2)) $((V1Y + V1H / 2)) wait 100 >/dev/null 2>&1
sleep 0.5
open p1 400 300
sleep 1
ids
P1U=$(uuid p1)
[ -n "$P1U" ] || stage_fail "p1 never mapped"
P1="org.pz.plain|$P1U"
G=$(geo p1)
F=$(floating "$P1")
echo "p1 reopened: $G floating=$F"
check "the reopen restores p1 at its spot on Virtual-1" "$(rect "$G" | cut -d' ' -f1,2)" = "$WX $WY"
check "the reopen restores p1 on Virtual-1" "$(outof "$G")" = Virtual-1
check "the reopen restores p1 floating" "$F" = true
if [ "$F" = true ]; then
    PZ org.plasmazones.Control.snapWindowToZone "$P1" 1 Virtual-1 >/dev/null
    sleep 2
    echo "p1 snapped into Virtual-1 zone 1 first: $(geo p1)"
fi
activate p1
shortcut toggle_window_float
sleep 2
G=$(geo p1)
echo "p1 after the float shortcut: $G"
check "the float shortcut puts p1 at its spot on Virtual-1" "$(rect "$G" | cut -d' ' -f1,2)" = "$WX $WY"
check "p1 is on Virtual-1 after the float" "$(outof "$G")" = Virtual-1
check "p1 reads floating after the shortcut" "$(floating "$P1")" = true
finish
