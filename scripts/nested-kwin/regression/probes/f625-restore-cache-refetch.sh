#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: GPL-3.0-or-later
#
# f625-restore-cache-refetch: WindowTracking.pendingRestoresChanged fires once per change of the
# pending restores, and the effect refetches its instant-restore cache on it. Runner: 2 outputs,
# flag F (fakeinput puts the pointer on Virtual-0).
# Staging: two desktops, Virtual-0 COLS3, Snap Assist off, the pointer on Virtual-0. A dbus-monitor
#   counts pendingRestoresChanged for the whole run.
#   Part 2 (run first): rest3 (app org.pz.restthree) snapped into zone 3 and rest2 (org.pz.resttwo)
#   into zone 2, both closed; a throwaway rule is added so the effect refetches the cache through
#   the rules path ("Cached 2 pending restore geometries"). Then desktop 1 switches to Columns (2)
#   by assignment, and both apps open again on Virtual-0.
#   Part 1: next_layout, a switch to desktop 2, snappingDisabledDesktops ['Virtual-0/2'], a switch
#   back to desktop 1, Virtual-1 moved below Virtual-0 with kscreen-doctor, then 4 s idle.
# Asserts: part 2: one pendingRestoresChanged for the switch, the effect logs "Cached 0 pending
#   restore geometries" after it, no "Instant snap restore" at the reopen and neither window at its
#   old COLS3 zone rect. Part 1: one pendingRestoresChanged per action (a desktop switch's several
#   bus messages coalesce into one), none while idle.
# A FAIL means: the signal never fired or fired per bus message, or the effect kept its stale cache
#   and teleported the reopened apps into their old zone rects.
set -u
NEST="$1"
LOG="$2"
. "$(dirname "$0")/../lib.sh"
ZONE2="{93b382be-8b48-496b-b53c-88bef2c46787}"
ZONE3="{b14ed13c-17cd-4da2-ba0a-8dae3b61dba2}"
RULEID="{11111111-2222-4333-8444-5555555f6251}"
kwjs_ok
need_outputs 2
setting snapAssistEnabled false
newdesk 1 D2
assignd Virtual-0 1 "$COLS3"
sleep 1.5
echo "desktop $(curdesk)"
read -r V0X V0Y V0W V0H <<< "$(scr Virtual-0)"
fake abs $((V0X + V0W / 2)) $((V0Y + V0H / 2)) wait 100
MON="$NEST/f625-monitor.log"
mon_start "$MON" "type='signal',interface='org.plasmazones.WindowTracking',member='pendingRestoresChanged'"
mmark "$MON"
# signals: SIGS = pendingRestoresChanged since the last mark (after a 2 s settle), then a fresh
# mark. Call it directly, never in $( ), or the mark is lost.
signals() {
    sleep 2
    SIGS=$(msgs "$MON" | grep -c "^pendingRestoresChanged")
    mmark "$MON"
}
echo "== part 2: the effect's cache after a layout switch"
assignd Virtual-0 1 "$COLS3"
assign Virtual-0 "$COLS3"
sleep 1.5
case "$(PZ org.plasmazones.LayoutRegistry.getLayoutForScreen Virtual-0)" in *"$COLS3"*) ;; *) stage_fail "Virtual-0 is not on COLS3" ;; esac
openapp rest3 org.pz.restthree 300 300
openapp rest2 org.pz.resttwo 300 300
for c in rest3 rest2; do
    sendto "$c" Virtual-0 || stage_fail "$c not on Virtual-0 ($(geo "$c"))"
done
ids
R3="org.pz.restthree|$(uuid rest3)"
R2="org.pz.resttwo|$(uuid rest2)"
PZ org.plasmazones.Snap.moveWindowToZone "$R3" "$ZONE3" >/dev/null
PZ org.plasmazones.Snap.moveWindowToZone "$R2" "$ZONE2" >/dev/null
sleep 2
G3=$(rect "$(geo rest3)")
G2=$(rect "$(geo rest2)")
echo "rest3 in zone 3: $G3 | rest2 in zone 2: $G2"
[ "$(zoneof "$R3")" = "$ZONE3" ] || stage_fail "rest3 not in zone 3"
[ "$(zoneof "$R2")" = "$ZONE2" ] || stage_fail "rest2 not in zone 2"
on rest3 'w[i].closeWindow();'
on rest2 'w[i].closeWindow();'
sleep 2
emark
RULE='{"actions":[{"value":true,"type":"setHideTitleBar"}],"enabled":true,"id":"'"$RULEID"'","match":{"all":[{"field":"appId","op":"equals","value":"org.pz.nothing"}]},"name":"probe F625 refetch","priority":50}'
echo "addRule: $(PZ org.plasmazones.Rules.addRule "$RULE")"
on_exit "PZ org.plasmazones.Rules.removeRule '$RULEID' >/dev/null"
sleep 2
[ "$(elog "Cached [0-9]* pending restore geometries" | tail -1)" = "Cached 2 pending restore geometries" ] \
    || stage_fail "the rule refetch did not cache the two closed apps' restores"
mmark "$MON"
emark
assignd Virtual-0 1 "$COLS2"
signals
check "the layout switch emits one pendingRestoresChanged" "$SIGS" = 1
case "$(PZ org.plasmazones.LayoutRegistry.getLayoutForScreen Virtual-0)" in *"$COLS2"*) ;; *) stage_fail "Virtual-0 did not switch to Columns (2)" ;; esac
check "the effect refetches an empty cache after the switch" \
    "$(elog "Cached 0 pending restore geometries" | wc -l)" -ge 1
fake abs $((V0X + V0W / 2)) $((V0Y + V0H / 2)) wait 100
emark
openapp rest3 org.pz.restthree 300 300
openapp rest2 org.pz.resttwo 300 300
sleep 2
N3=$(rect "$(geo rest3)")
N2=$(rect "$(geo rest2)")
echo "reopened: rest3 $N3 | rest2 $N2"
[ -n "$N3" ] && [ -n "$N2" ] || stage_fail "the apps did not reopen"
check "no instant snap restore at the reopen" "$(elog "Instant snap restore for " | wc -l)" = 0
check "rest3 does not reopen at its old zone-3 rect" "$N3" != "$G3"
check "rest2 does not reopen at its old zone-2 rect" "$N2" != "$G2"
PZ org.plasmazones.Rules.removeRule "$RULEID" >/dev/null
echo "== part 1: one signal per action"
signals
note "settle after staging: $SIGS signal(s)"
shortcut next_layout
signals
check "next_layout emits one pendingRestoresChanged" "$SIGS" = 1
js 'workspace.currentDesktop = workspace.desktops[1];'
signals
check "a switch to desktop 2 emits one pendingRestoresChanged" "$SIGS" = 1
echo "  setSetting snappingDisabledDesktops: $(gsetting snappingDisabledDesktops "<['Virtual-0/2']>")"
signals
check "disabling snapping on Virtual-0/2 emits one pendingRestoresChanged" "$SIGS" = 1
js 'workspace.currentDesktop = workspace.desktops[0];'
signals
check "the switch back to desktop 1 emits one pendingRestoresChanged" "$SIGS" = 1
WAYLAND_DISPLAY="$WAYLAND_DISPLAY" kscreen-doctor "output.Virtual-1.position.0,$V0H" >/dev/null 2>&1
echo "  outputs after moving Virtual-1 below Virtual-0: $(screens)"
signals
check "the output rearrangement emits one pendingRestoresChanged" "$SIGS" = 1
sleep 2
signals
check "4 s idle emits no pendingRestoresChanged" "$SIGS" = 0
echo "  setSetting snappingDisabledDesktops []: $(gsetting snappingDisabledDesktops "<@as []>")"
signals
note "re-enabling Virtual-0/2: $SIGS signal(s)"
finish
