#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: GPL-3.0-or-later
#
# f1012-disable-rule-renumber: a disable rule the settings minted, moved by a desktop renumber, is
# renamed with its desktop; a rule the user renamed keeps its name. Runner: 1 output, no flags.
# Staging: three desktops. Autotile, snapping and scrolling are each switched off on Virtual-0
#   desktop 3 through the settings (autotileDisabledDesktops, snappingDisabledDesktops,
#   scrollingDisabledDesktops), which mints three disable rules; the scrolling one is renamed with
#   Rules.updateRule ("probe F1012 my scroll rule"). KWin removes desktop 2, so desktop 3 becomes 2.
# Asserts: the three rules all match desktop 2; the minted ones read "Autotile off · Virtual-0 ·
#   Desktop 2" and "Snapping off · Virtual-0 · Desktop 2" and none still says Desktop 3; the renamed
#   one keeps its name; the three disabled-desktops settings read Virtual-0/2.
# A FAIL means: the rules moved with the renumber but the minted names still named desktop 3.
set -u
NEST="$1"
LOG="$2"
. "$(dirname "$0")/../lib.sh"
kwjs_ok
RULES="$XDG_CONFIG_HOME/plasmazones/rules.json"
# offrules: every rule whose name says "off ·" or "probe F1012", as "NAME|vd=[LEAVES]" lines
offrules() {
    python3 -c '
import json, sys
try:
    d = json.load(open(sys.argv[1]))
except Exception as e:
    print("unreadable:", e); sys.exit()
rules = d.get("rules", []) if isinstance(d, dict) else d
def leaves(m, out):
    if isinstance(m, dict):
        if m.get("field") == "virtualDesktop":
            out.append(m.get("value"))
        for v in m.values():
            leaves(v, out)
    elif isinstance(m, list):
        for v in m:
            leaves(v, out)
for r in rules:
    name = r.get("name", "")
    if " off · " in name or "probe F1012" in name:
        vd = []
        leaves(r.get("match", {}), vd)
        print("%s|vd=%s" % (name, vd))
' "$RULES"
}
for n in 1 2; do
    newdesk "$n" "D$((n + 1))"
done
sleep 1.5
for k in autotileDisabledDesktops snappingDisabledDesktops scrollingDisabledDesktops; do
    echo "$k off on desktop 3: $(gsetting "$k" "<['Virtual-0/3']>")"
    sleep 0.5
done
sleep 1.5
SCROLLRULE=$(python3 -c '
import json, sys
d = json.load(open(sys.argv[1]))
rules = d.get("rules", []) if isinstance(d, dict) else d
for r in rules:
    if r.get("name", "").startswith("Scrolling off · ") and r.get("name", "").endswith("Desktop 3"):
        r["name"] = "probe F1012 my scroll rule"
        print(json.dumps(r))
        break
' "$RULES" 2>/dev/null)
[ -n "$SCROLLRULE" ] || stage_fail "no minted 'Scrolling off · ... Desktop 3' rule to rename"
[ "$(PZ org.plasmazones.Rules.updateRule "$SCROLLRULE")" = true ] || stage_fail "Rules.updateRule refused the rename"
sleep 1.5
echo "before the removal:"
offrules | sed 's/^/  /'
[ "$(offrules | grep -c "|vd=\[3\]$")" = 3 ] || stage_fail "the three rules do not all match desktop 3 before the removal"
mapfile -t IDS < <(deskids)
[ "${#IDS[@]}" -ge 3 ] || stage_fail "${#IDS[@]} desktops, the probe needs 3"
echo "removing desktop 2 (${IDS[1]})"
qdbus6 org.kde.KWin /VirtualDesktopManager org.kde.KWin.VirtualDesktopManager.removeDesktop "${IDS[1]}" >/dev/null
sleep 3
echo "now on: $(curdesk)"
echo "after the removal:"
offrules | sed 's/^/  /'
check "rules matching desktop 2 after the removal" "$(offrules | grep -c "|vd=\[2\]$")" -eq 3
check "the autotile rule's name" "$(offrules | grep -c "^Autotile off · Virtual-0 · Desktop 2|")" -eq 1
check "the snapping rule's name" "$(offrules | grep -c "^Snapping off · Virtual-0 · Desktop 2|")" -eq 1
check "the renamed rule keeps its name" "$(offrules | grep -c "^probe F1012 my scroll rule|vd=\[2\]$")" -eq 1
check "minted names still saying Desktop 3" "$(offrules | grep -c " off · Virtual-0 · Desktop 3|")" -eq 0
for k in autotileDisabledDesktops snappingDisabledDesktops scrollingDisabledDesktops; do
    check "$k after the removal" "$(PZ org.plasmazones.Settings.getSetting "$k" | tr '\n' ' ' | sed 's/ *$//')" = "Virtual-0/2"
done
finish
