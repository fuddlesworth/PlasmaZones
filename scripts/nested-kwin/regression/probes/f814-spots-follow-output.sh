#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: GPL-3.0-or-later
#
# f814-spots-follow-output: a remembered floating spot moves with its output when the outputs are
# rearranged, and is saved relative to its output. Runner: 2 outputs, flag S. It also leaves the
# state home f814-login-relative-spot runs on next.
# Staging: Virtual-0 at 0,0 and Virtual-1 at 1600,0, both COLS3, Snap Assist off. p1 is sent to
#   Virtual-1, placed free at 200,200 on it and snapped into its zone 1, so its float-back spot is
#   filed under Virtual-1. kscreen-doctor swaps the outputs (Virtual-1 to 0,0, Virtual-0 to 1600,0)
#   and the float shortcut is pressed on p1. For the login half p1 is then moved to 300,250 on
#   Virtual-1 and the daemon is stopped with its activation kept blocked, so the compositor's
#   teardown cannot touch session.json.
# Asserts: the daemon logs "Output Virtual-1 now at QRect(0,0 WxH) - moved the floating spots of 1
#   window(s)"; the float shortcut floats p1 at 200,200 on Virtual-1; the saved placements carry
#   "_version" 2 and, after the stop, p1's freeGeo Virtual-1: 300,250 (relative to its output).
# A FAIL means: the spot stayed absolute, so after the swap it pointed at Virtual-0 and the float
#   left p1 on its zone rect.
set -u
NEST="$1"
LOG="$2"
. "$(dirname "$0")/../lib.sh"
kwjs_ok
need_outputs 2
read -r V1X V1Y V1W V1H <<< "$(scr Virtual-1)"
setting snapAssistEnabled false
assign Virtual-0 "$COLS3"
assign Virtual-1 "$COLS3"
sleep 2
open p1 400 300
sendto p1 Virtual-1 || stage_fail "p1 not on Virtual-1 ($(geo p1))"
moveto p1 $((V1X + 200)) $((V1Y + 200))
sleep 1.5
ids
P1U=$(uuid p1)
[ -n "$P1U" ] || stage_fail "p1 never mapped"
P1="org.pz.plain|$P1U"
G=$(geo p1)
echo "p1 free: $G"
[ "$(rect "$G" | cut -d' ' -f1,2)" = "$((V1X + 200)) $((V1Y + 200))" ] && [ "$(outof "$G")" = Virtual-1 ] \
    || stage_fail "p1 not free at 200,200 on Virtual-1 ($G)"
PZ org.plasmazones.Control.snapWindowToZone "$P1" 1 Virtual-1 >/dev/null
sleep 2
case "$(zoneof "$P1")" in "{"*) ;; *) stage_fail "p1 did not snap into Virtual-1 zone 1" ;; esac
echo "p1 snapped: $(geo p1)"
dmark
ksd output.Virtual-1.position.0,0 output.Virtual-0.position.$V1W,0
sleep 4
SCR=$(screens)
echo "outputs after the swap: $SCR"
[ "$(scr Virtual-1)" = "0 0 $V1W $V1H" ] && [ "$(scr Virtual-0 | cut -d' ' -f1,2)" = "$V1W 0" ] \
    || stage_fail "kscreen-doctor did not swap the outputs ($SCR)"
check "the daemon moved Virtual-1's floating spots with it" \
    "$(dlog "Output \"Virtual-1\" now at QRect\\(0,0 ${V1W}x${V1H}\\) - moved the floating spots of 1 window" | wc -l)" -ge 1
activate p1
shortcut toggle_window_float
sleep 2
G=$(geo p1)
echo "p1 after the float shortcut: $G"
check "p1 floats at its spot, 200,200 on Virtual-1" "$(rect "$G" | cut -d' ' -f1,2)" = "200 200"
check "p1 is on Virtual-1 after the float" "$(outof "$G")" = Virtual-1
check "p1 reads floating" "$(floating "$P1")" = true
# The live spot the login half reads.
moveto p1 300 250
sleep 2
echo "p1 moved for the login half: $(geo p1)"
P=""
for _ in $(seq 20); do
    P=$(placements "$P1U")
    case "$P" in *"Virtual-1: 300,250 "*) break ;; esac
    sleep 1
done
echo "saved placements: $P"
check "the saved placements carry _version 2" "$P" '~' "^_version=2 "
dkill
# Keep activation blocked past finish: the teardown must not start a daemon that rewrites the file.
on_exit 'printf "#!/bin/sh\nexit 1\n" > "$NEST/activate-daemon.sh"'
P=$(placements "$P1U")
echo "placements on disk after the stop: $P"
check "p1's spot is saved relative to Virtual-1" "$P" '~' "freeGeo=\\{[^}]*Virtual-1: 300,250 "
finish
