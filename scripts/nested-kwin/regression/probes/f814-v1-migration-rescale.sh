#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: GPL-3.0-or-later
#
# f814-v1-migration-rescale: the one-time v1 to v2 migration of the saved floating spots, then an
# output resize that leaves a spot off its output. Runner: 2 outputs, flag S.
# Staging: Virtual-0 at 0,0 and Virtual-1 at W,0, both COLS3, Snap Assist off. p1 is sent to
#   Virtual-1, placed free at 1150,500 on it and snapped into its zone 1; the save is awaited.
#   Migration: the daemon is stopped, session.json's placements are rewritten to the old format
#   ("_version" removed, p1's freeGeo set to the absolute W+1150,500, plus an entry under "Gone-9",
#   an output not connected) and the daemon is started again; the float shortcut floats p1.
#   Resize: p1 is snapped back into zone 1 and Virtual-1 is scaled to 2 (logical W/2 x H/2 at W,0,
#   so the spot is off it); the float shortcut floats p1 again.
# Asserts: the first save carries "_version" 2 and Virtual-1: 1150,500; the restart logs
#   "migrateV1ToV2: dropped 1 remembered floating position(s)" and the file then reads "_version" 2,
#   Virtual-1: 1150,500 and no Gone-9; the float puts p1 at W+1150,500. After the scale the daemon
#   logs "Output Virtual-1 now at QRect(W,0 W/2xH/2) - moved the floating spots of 1 window(s)" and
#   the float puts p1 inside Virtual-1's bottom-right corner at the same size.
# A FAIL means: the file kept the absolute format and the dead output's entry, or the float after
#   the scale left p1 on its zone rect because the spot was off the output.
set -u
NEST="$1"
LOG="$2"
. "$(dirname "$0")/../lib.sh"
SESSION="$XDG_CONFIG_HOME/plasmazones/session.json"
kwjs_ok
need_outputs 2
read -r V1X V1Y V1W V1H <<< "$(scr Virtual-1)"
AX=$((V1X + 1150))
AY=$((V1Y + 500))
setting snapAssistEnabled false
assign Virtual-0 "$COLS3"
assign Virtual-1 "$COLS3"
sleep 2
open p1 400 300
sendto p1 Virtual-1 || stage_fail "p1 not on Virtual-1 ($(geo p1))"
moveto p1 "$AX" "$AY"
sleep 1.5
ids
P1U=$(uuid p1)
[ -n "$P1U" ] || stage_fail "p1 never mapped"
P1="org.pz.plain|$P1U"
G=$(geo p1)
echo "p1 free: $G"
[ "$(rect "$G" | cut -d' ' -f1,2)" = "$AX $AY" ] && [ "$(outof "$G")" = Virtual-1 ] \
    || stage_fail "p1 not free at 1150,500 on Virtual-1 ($G)"
PZ org.plasmazones.Control.snapWindowToZone "$P1" 1 Virtual-1 >/dev/null
sleep 2
case "$(zoneof "$P1")" in "{"*) ;; *) stage_fail "p1 did not snap into Virtual-1 zone 1" ;; esac
P=""
for _ in $(seq 15); do
    P=$(placements "$P1U")
    case "$P" in *"freeGeo={"?*) break ;; esac
    sleep 1
done
echo "saved placements: $P"
check "the first save carries _version 2" "$P" '~' "^_version=2 "
check "the first save holds p1's spot relative to Virtual-1" "$P" '~' "Virtual-1: 1150,500 "
echo "== the migration"
dkill
python3 -c '
import json, sys
path, uid, ax, ay = sys.argv[1], sys.argv[2], int(sys.argv[3]), int(sys.argv[4])
d = json.load(open(path))
def holder(o):
    if isinstance(o, dict):
        if "WindowPlacements" in o:
            return o
        for v in o.values():
            r = holder(v)
            if r is not None:
                return r
    return None
h = holder(d)
if h is None:
    print("no WindowPlacements to rewrite"); sys.exit()
wp = h["WindowPlacements"]
as_string = isinstance(wp, str)
if as_string:
    wp = json.loads(wp)
wp.pop("_version", None)
n = 0
for app, recs in wp.items():
    if not isinstance(recs, list):
        continue
    for r in recs:
        if uid not in r.get("windowId", ""):
            continue
        fg = r.setdefault("freeGeo", {})
        for k in list(fg.keys()):
            fg[k] = dict(fg[k], x=ax, y=ay)
        fg["Gone-9"] = {"x": 100, "y": 100, "width": 400, "height": 300}
        n += 1
h["WindowPlacements"] = json.dumps(wp) if as_string else wp
json.dump(d, open(path, "w"))
print("rewrote %d record(s) to the old absolute format" % n)
' "$SESSION" "$P1U" "$AX" "$AY"
P=$(placements "$P1U")
echo "on disk before the start: $P"
case "$P" in "_version=absent "*"Virtual-1: $AX,$AY "*"Gone-9"*) ;; *) stage_fail "the rewrite to the v1 format did not hold" ;; esac
drestart
sleep 4
check "the start migrates and drops the disconnected output's spot" \
    "$(grep -c "migrateV1ToV2: dropped 1 remembered floating position" "$NEST/daemon.log")" -ge 1
sleep 4
P=$(placements "$P1U")
echo "on disk after the save debounce: $P"
check "the migrated file carries _version 2" "$P" '~' "^_version=2 "
check "the migrated file holds p1's spot relative to Virtual-1" "$P" '~' "Virtual-1: 1150,500 "
check "the migrated file drops the disconnected output's entry" "$P" '!~' "Gone-9"
floatkey p1
G=$(geo p1)
echo "p1 after the float shortcut: $G"
check "the float puts p1 at its spot after the migration" "$(rect "$G" | cut -d' ' -f1,2)" = "$AX $AY"
check "p1 reads floating after the migration" "$(floating "$P1")" = true
echo "== the resize"
PZ org.plasmazones.Control.snapWindowToZone "$P1" 1 Virtual-1 >/dev/null
sleep 2
case "$(zoneof "$P1")" in "{"*) ;; *) stage_fail "p1 did not snap back into Virtual-1 zone 1" ;; esac
dmark
ksd output.Virtual-1.scale.2
on_exit 'ksd output.Virtual-1.scale.1 >/dev/null'
sleep 4
SCR=$(screens)
echo "outputs after the scale: $SCR"
read -r S1X S1Y S1W S1H <<< "$(scr Virtual-1)"
[ "$S1X $S1Y $S1W $S1H" = "$V1X $V1Y $((V1W / 2)) $((V1H / 2))" ] || stage_fail "the scale did not halve Virtual-1 ($SCR)"
check "the daemon moved Virtual-1's floating spots with the resize" \
    "$(dlog "Output \"Virtual-1\" now at QRect\\($S1X,$S1Y ${S1W}x${S1H}\\) - moved the floating spots of 1 window" | wc -l)" -ge 1
floatkey p1
G=$(geo p1)
echo "p1 after the float shortcut: $G"
read -r GX GY GW GH _ <<< "$G"
check "the float pulls p1 into Virtual-1's bottom-right corner (x)" "${GX%.*}" = "$((S1X + S1W - ${GW%.*}))"
check "the float pulls p1 into Virtual-1's bottom-right corner (y)" "${GY%.*}" = "$((S1Y + S1H - ${GH%.*}))"
check "p1 keeps its free size" "${GW%.*}" = 400
check "p1 is on Virtual-1 after the scale" "$(outof "$G")" = Virtual-1
check "p1 reads floating after the scale" "$(floating "$P1")" = true
finish
