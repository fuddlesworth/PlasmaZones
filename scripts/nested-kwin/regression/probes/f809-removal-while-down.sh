#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: GPL-3.0-or-later
#
# f809-removal-while-down: a desktop removed while the daemon was down is renumbered at the next
# start, from the desktop ids the session remembers. Runner: 1 output, no flags.
# Staging: one output, four desktops; desktop 2 autotile:bsp, desktop 3 COLS3, desktop 4 DUP (a
#   COLS3 duplicate). After a save tick the daemon is stopped, KWin removes desktop 2, and the
#   daemon is started again on the same home. Last, KnownDesktopIds is deleted from session.json
#   and the LAST desktop is removed while the daemon is down (the count sweep).
# Asserts: session.json's KnownDesktopIds holds the four ids before the stop; after the restart
#   desktop 2 resolves COLS3 in snapping mode and desktop 3 DUP, rules.json names no desktop 4, and
#   KnownDesktopIds holds the three live ids; after the count sweep rules.json names no desktop past
#   the new last one.
# A FAIL means: the removal went unseen, so desktop 2 resolved the removed desktop's autotile rule,
#   desktop 3 kept COLS3 and the DUP rule lingered at 4.
set -u
NEST="$1"
LOG="$2"
. "$(dirname "$0")/../lib.sh"
kwjs_ok
RULES="$XDG_CONFIG_HOME/plasmazones/rules.json"
SESSION="$XDG_CONFIG_HOME/plasmazones/session.json"
# known -> session.json's KnownDesktopIds, comma-separated
known() {
    python3 -c '
import json, sys
def find(o):
    if isinstance(o, dict):
        for k, v in o.items():
            if k == "KnownDesktopIds":
                return v
            r = find(v)
            if r is not None:
                return r
    return None
try:
    v = find(json.load(open(sys.argv[1])))
except Exception as e:
    v = "unreadable: %s" % e
if isinstance(v, list):
    v = ",".join(v)
print(v if v is not None else "(no KnownDesktopIds)")
' "$SESSION"
}
# restart TAG: start the daemon again, keeping the previous log (daemon.sh truncates it)
restart() {
    cp "$NEST/daemon.log" "$NEST/daemon.log.f809-$1"
    drestart
    sleep 4
}
for n in 1 2 3; do
    newdesk "$n" "D$((n + 1))"
done
sleep 1
DUP=$(PZ org.plasmazones.LayoutRegistry.duplicateLayout "$COLS3")
echo "DUP layout: $DUP"
case "$DUP" in "{"*) ;; *) stage_fail "duplicateLayout returned no id ($DUP)" ;; esac
sleep 1
assignd Virtual-0 2 "autotile:bsp"
assignd Virtual-0 3 "$COLS3"
assignd Virtual-0 4 "$DUP"
sleep 3
mapfile -t IDS < <(deskids)
[ "${#IDS[@]}" = 4 ] || stage_fail "KWin lists ${#IDS[@]} desktops, not 4"
echo "KWin ids: ${IDS[*]}"
echo "before the stop: 2=$(layoutof Virtual-0 2) 3=$(layoutof Virtual-0 3) 4=$(layoutof Virtual-0 4) | rules.json desktop leaves $(rule_desktops)"
[ "$(layoutof Virtual-0 2)" = "autotile:bsp" ] && [ "$(layoutof Virtual-0 4)" = "$DUP" ] || stage_fail "the per-desktop assignments did not take"
check "session.json KnownDesktopIds before the stop" "$(known)" = "$(printf '%s\n' "${IDS[@]}" | paste -sd,)"
dkill
echo "removing desktop 2 (${IDS[1]}) while the daemon is down"
qdbus6 org.kde.KWin /VirtualDesktopManager org.kde.KWin.VirtualDesktopManager.removeDesktop "${IDS[1]}" >/dev/null
sleep 2
restart pass1
echo "after the restart: 2=$(layoutof Virtual-0 2) 3=$(layoutof Virtual-0 3) | rules.json desktop leaves $(rule_desktops)"
check "desktop 2's layout after the restart" "$(layoutof Virtual-0 2)" = "$COLS3"
check "desktop 2's mode after the restart" "$(modeof Virtual-0 2)" = 0
check "desktop 3's layout after the restart" "$(layoutof Virtual-0 3)" = "$DUP"
check "rules.json desktop leaves after the restart" "$(rule_desktops)" = "2,3"
sleep 1.5
check "session.json KnownDesktopIds after the restart" "$(known)" = "$(deskids | paste -sd,)"

echo "== the count sweep: KnownDesktopIds deleted, the last desktop removed while down"
mapfile -t IDS < <(deskids)
LASTN=${#IDS[@]}
assignd Virtual-0 "$LASTN" "$COLS3"
sleep 2
echo "rules.json desktop leaves before (the last desktop is $LASTN): $(rule_desktops)"
case ",$(rule_desktops)," in *",$LASTN,"*) ;; *) stage_fail "no rule names the last desktop $LASTN before the sweep" ;; esac
dkill
python3 -c '
import json, sys
p = sys.argv[1]
d = json.load(open(p))
def drop(o):
    if isinstance(o, dict):
        o.pop("KnownDesktopIds", None)
        for v in o.values():
            drop(v)
drop(d)
json.dump(d, open(p, "w"))
' "$SESSION"
[ "$(known)" = "(no KnownDesktopIds)" ] || stage_fail "KnownDesktopIds could not be removed from session.json"
echo "removing the last desktop (${IDS[$((LASTN - 1))]}) while the daemon is down"
qdbus6 org.kde.KWin /VirtualDesktopManager org.kde.KWin.VirtualDesktopManager.removeDesktop "${IDS[$((LASTN - 1))]}" >/dev/null
sleep 2
restart pass2
echo "rules.json desktop leaves after the sweep: $(rule_desktops)"
check "rules naming the removed last desktop $LASTN after the sweep" "$(rule_desktops | tr ',' '\n' | grep -c "^$LASTN\$")" -eq 0
finish
