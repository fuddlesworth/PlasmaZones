#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: GPL-3.0-or-later
#
# f786-desktop-removal-renumber: removing a desktop renumbers the assignments, the per-desktop
# disables and the window rules above it, and a desktop inserted at the front moves them up.
# Runner: 1 output, no flags.
# Staging: one output, three desktops. Desktop 1 COLS3, desktop 2 autotile:bsp, desktop 3 a
#   duplicate of COLS3 (DUP); autotile switched off on desktop 3. Two window rules: appId kwrite
#   and virtualDesktop equals 3 (hide title bar), and appId konsole routeToDesktop 2. KWin removes
#   desktop 2 (desktop 3 becomes 2), then a desktop is inserted at the front.
# Asserts: after the removal desktop 2 resolves DUP in snapping mode, autotileDisabledDesktops is
#   Virtual-0/2, rules.json names no desktop 3, the kwrite rule names desktop 2 and is enabled, the
#   konsole route rule still names 2 and is disabled, and the daemon logs "Virtual desktops
#   renumbered"; after the front insert DUP is on desktop 3 and both window rules name 3.
# A FAIL means: the removed desktop's rules stayed in place, so new desktop 2 resolved autotile,
#   the rules at 3 stayed at 3 and the insert moved nothing.
set -u
NEST="$1"
LOG="$2"
. "$(dirname "$0")/../lib.sh"
kwjs_ok
for n in 1 2; do
    newdesk "$n" "D$((n + 1))"
done
sleep 1
DUP=$(PZ org.plasmazones.LayoutRegistry.duplicateLayout "$COLS3")
echo "DUP layout: $DUP"
case "$DUP" in "{"*) ;; *) stage_fail "duplicateLayout returned no id ($DUP)" ;; esac
sleep 1
assignd Virtual-0 1 "$COLS3"
assignd Virtual-0 2 "autotile:bsp"
assignd Virtual-0 3 "$DUP"
gsetting autotileDisabledDesktops "<['Virtual-0/3']>" >/dev/null
R1='{"actions":[{"value":true,"type":"setHideTitleBar"}],"enabled":true,"id":"{11111111-2222-4333-8444-5555555d3001}","match":{"all":[{"field":"appId","op":"equals","value":"kwrite"},{"field":"virtualDesktop","op":"equals","value":3}]},"name":"probe D3 kwrite on 3","priority":50}'
R2='{"actions":[{"targetDesktop":2,"type":"routeToDesktop"}],"enabled":true,"id":"{11111111-2222-4333-8444-5555555d3002}","match":{"all":[{"field":"appId","op":"equals","value":"konsole"}]},"name":"probe D3 konsole to 2","priority":50}'
[ "$(PZ org.plasmazones.Rules.addRule "$R1")" = true ] || stage_fail "the kwrite rule was not added"
[ "$(PZ org.plasmazones.Rules.addRule "$R2")" = true ] || stage_fail "the konsole rule was not added"
sleep 2
echo "before the removal: 1=$(layoutof Virtual-0 1) 2=$(layoutof Virtual-0 2) 3=$(layoutof Virtual-0 3)"
rule_rows | sed 's/^/  /'
[ "$(layoutof Virtual-0 2)" = "autotile:bsp" ] && [ "$(layoutof Virtual-0 3)" = "$DUP" ] || stage_fail "the per-desktop assignments did not take"
[ "$(rule_row "probe D3 kwrite on 3")" = "True|3|" ] || stage_fail "the kwrite rule is not stored at desktop 3"
mapfile -t IDS < <(deskids)
[ "${#IDS[@]}" = 3 ] || stage_fail "KWin lists ${#IDS[@]} desktops, not 3"
dmark
echo "removing desktop 2 (${IDS[1]})"
qdbus6 org.kde.KWin /VirtualDesktopManager org.kde.KWin.VirtualDesktopManager.removeDesktop "${IDS[1]}" >/dev/null
sleep 2.5
desk 2
echo "now on: $(curdesk)"
check "getLayoutForScreen Virtual-0 on the new desktop 2" "$(PZ org.plasmazones.LayoutRegistry.getLayoutForScreen Virtual-0)" = "$DUP"
check "desktop 2's layout after the removal" "$(layoutof Virtual-0 2)" = "$DUP"
check "desktop 2's mode after the removal" "$(modeof Virtual-0 2)" = 0
check "autotileDisabledDesktops after the removal" \
    "$(PZ org.plasmazones.Settings.getSetting autotileDisabledDesktops | tr -d '\n ')" = "Virtual-0/2"
rule_rows | sed 's/^/  /'
check "rules naming desktop 3 after the removal" "$(rule_rows | cut -d'|' -f3 | grep -c '\b3\b')" -eq 0
check "the kwrite rule after the removal" "$(rule_row "probe D3 kwrite on 3")" = "True|2|"
check "the konsole route rule after the removal" "$(rule_row "probe D3 konsole to 2")" = "False||2"
check "'Virtual desktops renumbered' daemon lines after the removal" "$(dlog "Virtual desktops renumbered" | wc -l)" -ge 1
desk 1
dmark
echo "inserting a desktop at the front"
qdbus6 org.kde.KWin /VirtualDesktopManager org.kde.KWin.VirtualDesktopManager.createDesktop 0 X >/dev/null
sleep 2.5
echo "after the front insert: 1=$(layoutof Virtual-0 1) 2=$(layoutof Virtual-0 2) 3=$(layoutof Virtual-0 3)"
rule_rows | sed 's/^/  /'
check "desktop 3's layout after the front insert" "$(layoutof Virtual-0 3)" = "$DUP"
check "desktop 2's layout after the front insert" "$(layoutof Virtual-0 2)" = "$COLS3"
check "the kwrite rule after the front insert" "$(rule_row "probe D3 kwrite on 3")" = "True|3|"
check "the konsole route rule after the front insert" "$(rule_row "probe D3 konsole to 2")" = "False||3"
finish
