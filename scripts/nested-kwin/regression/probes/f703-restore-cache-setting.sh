#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: GPL-3.0-or-later
#
# f703-restore-cache-setting: with restoreWindowsToZonesOnLogin off, the effect's instant-restore
# cache is empty and a reopened app is not teleported onto its old zone. Runner: 1 output, no flags.
# Staging: one output on COLS3, Snap Assist off. Four apps of their own (appid_gtk.py): pa and ra
#   snapped into zone 1, qa and sa into zone 3, then all four closed, so each leaves a closed
#   snapped record (getPendingRestoreGeometries lists them). restoreWindowsToZonesOnLogin is set
#   false, the settings saved and the daemon restarted (the effect fetches the cache at
#   daemon-ready), then pa is reopened.
# Asserts: the effect logs "Cached 0 pending restore geometries" after the restart,
#   getPendingRestoreGeometries answers {}, and pa opens off the zone 1 rect with no zone.
# A FAIL means: the cache kept the records, so pa was moved onto its old zone rect with no zone
#   behind it although the restore itself said no.
set -u
NEST="$1"
LOG="$2"
. "$(dirname "$0")/../lib.sh"
kwjs_ok
setting snapAssistEnabled false
assign Virtual-0 "$COLS3"
sleep 2
Z1=$(zoneid "$COLS3" 1)
Z3=$(zoneid "$COLS3" 3)
[ -n "$Z1" ] && [ -n "$Z3" ] || stage_fail "COLS3's zone ids did not resolve"
# wid APP -> the full window id of the appid_gtk.py window captioned APP (app id org.pz.APP)
wid() {
    ids
    echo "org.pz.$1|$(uuid "$1")"
}
pending() { PZ org.plasmazones.WindowTracking.getPendingRestoreGeometries | tr -d '\n '; }
close() {
    on "$1" "w[i].closeWindow();"
    sleep 1.5
}
for c in pa qa ra sa; do openapp "$c" "org.pz.$c" 300 300; done
for c in pa ra; do
    PZ org.plasmazones.Snap.moveWindowToZone "$(wid "$c")" "$Z1" >/dev/null
    sleep 1.5
done
for c in qa sa; do
    PZ org.plasmazones.Snap.moveWindowToZone "$(wid "$c")" "$Z3" >/dev/null
    sleep 1.5
done
Z1RECT=$(rect "$(geo pa)")
echo "zone 1 rect (pa snapped): $Z1RECT | zone 3 rect (qa snapped): $(rect "$(geo qa)")"
case "$(zoneof "$(wid pa)")" in "{"*) ;; *) stage_fail "pa did not snap into zone 1" ;; esac
for c in pa qa ra sa; do close "$c"; done
P=$(pending)
echo "cache with all four closed: $(echo "$P" | cut -c1-200)"
for c in pa qa ra sa; do
    case "$P" in *"\"org.pz.$c\""*) ;; *) stage_fail "no closed record for org.pz.$c before the restart" ;; esac
done

setting restoreWindowsToZonesOnLogin false
PZ org.plasmazones.Settings.saveSettings >/dev/null
sleep 1
emark
drestart
sleep 7
[ "$(PZ org.plasmazones.Settings.getSetting restoreWindowsToZonesOnLogin)" = false ] \
    || stage_fail "restoreWindowsToZonesOnLogin did not read back false after the restart"
check "the effect's restore cache after the restart" \
    "$(elog "Cached [0-9]* pending restore geometries" | tail -1)" = "Cached 0 pending restore geometries"
check "getPendingRestoreGeometries after the restart" "$(pending)" = "{}"
openapp pa org.pz.pa 300 300
PA=$(wid pa)
G=$(geo pa)
[ -n "$G" ] || stage_fail "pa never reopened"
echo "pa reopened: $G"
check "pa's frame after the reopen is not the zone 1 rect" "$(rect "$G")" != "$Z1RECT"
check "pa's zone after the reopen" "$(zoneof "$PA")" = ""
finish
