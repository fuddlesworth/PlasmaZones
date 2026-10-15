#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: GPL-3.0-or-later
#
# f1092-desktop-rule-renumber: a desktop-only window rule is switched off when its desktop goes,
# not deleted, and is renumbered with its desktop. Runner: 1 output, no flags.
# Staging: three desktops. Rule R1 "virtual desktop equals 3" -> float, enabled. Run 1: desktop 3
#   removed. Run 2: a third desktop is created again, rule R2 "virtual desktop equals 3" -> float is
#   added, and desktop 2 removed. The rule store is read through Rules.getAllRules.
# Asserts: after run 1, R1 is present, disabled and still names desktop 3; after run 2, R2 is present,
#   enabled and names desktop 2.
# A FAIL means: removing the desktop deleted the rule outright.
set -u
NEST="$1"
LOG="$2"
. "$(dirname "$0")/../lib.sh"
kwjs_ok
newdesk 1 D2
newdesk 2 D3
echo "desktops: $(curdesk)"
case "$(curdesk)" in *"of 3") ;; *) stage_fail "the session does not have three desktops" ;; esac
R1="{11111111-2222-4333-8444-55555f109201}"
R2="{11111111-2222-4333-8444-55555f109202}"
on_exit 'PZ org.plasmazones.Rules.removeRule "$R1" >/dev/null; PZ org.plasmazones.Rules.removeRule "$R2" >/dev/null'
rule() { echo '{"actions":[{"value":true,"type":"float"}],"enabled":true,"id":"'"$1"'","match":{"all":[{"field":"virtualDesktop","op":"equals","value":3}]},"name":"'"$2"'","priority":50}'; }
echo "addRule R1: $(PZ org.plasmazones.Rules.addRule "$(rule "$R1" "probe F1092 desktop 3")")"
sleep 1
[ "$(rule_state "$R1")" = "present enabled=True desktop=[3]" ] || stage_fail "R1 is not in the store as added ($(rule_state "$R1"))"
echo "== run 1: desktop 3 removed"
js 'workspace.removeDesktop(workspace.desktops[2]);'
sleep 3
echo "  desktops: $(curdesk)"
check "R1 after desktop 3 is removed" "$(rule_state "$R1")" = "present enabled=False desktop=[3]"
echo "== run 2: a third desktop again, R2 naming 3, desktop 2 removed"
newdesk 2 D3b
sleep 1
echo "  desktops: $(curdesk)"
echo "  addRule R2: $(PZ org.plasmazones.Rules.addRule "$(rule "$R2" "probe F1092 desktop 3 shift")")"
sleep 1
[ "$(rule_state "$R2")" = "present enabled=True desktop=[3]" ] || stage_fail "R2 is not in the store as added ($(rule_state "$R2"))"
js 'workspace.removeDesktop(workspace.desktops[1]);'
sleep 3
echo "  desktops: $(curdesk)"
check "R2 after desktop 2 is removed" "$(rule_state "$R2")" = "present enabled=True desktop=[2]"
finish
