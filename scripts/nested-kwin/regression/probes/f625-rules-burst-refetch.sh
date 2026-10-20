#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: GPL-3.0-or-later
#
# f625-rules-burst-refetch: the effect refetches the instant-restore cache once per rules burst, on
# the same 50 ms debounce as its getAllRules (F625, rules half). Runner: 1 output, no flags.
# Staging: none beyond the default session. A dbus-monitor records Rules.rulesChanged and the
#   effect's getPendingRestoreGeometries and getAllRules calls. Burst 1: one Python process (one
#   connection) adds ten harmless rules back to back, one rulesChanged each; burst 2 removes them
#   the same way. A burst counts only when its largest gap between rulesChanged signals is under
#   50 ms (the debounce coalesces only those).
# Asserts: per counted burst rulesChanged x10, getAllRules x1 and getPendingRestoreGeometries x1.
# A FAIL means: every rules change refetched the restore cache.
set -u
NEST="$1"
LOG="$2"
. "$(dirname "$0")/../lib.sh"
kwjs_ok
MON="$NEST/e18c-monitor.log"
mon_start "$MON" "type='signal',interface='org.plasmazones.Rules',member='rulesChanged'" \
    "type='method_call',interface='org.plasmazones.WindowTracking',member='getPendingRestoreGeometries'" \
    "type='method_call',interface='org.plasmazones.Rules',member='getAllRules'"
sleep 0.5
on_exit 'burst remove >/dev/null 2>&1'
burst() {
    python3 -I - "$1" << 'EOF'
import sys

from gi.repository import Gio, GLib

mode = sys.argv[1]
bus = Gio.bus_get_sync(Gio.BusType.SESSION, None)
for i in range(10):
    rid = "{11111111-2222-4333-8444-5555555e18c%d}" % i
    if mode == "add":
        rule = ('{"actions":[{"value":true,"type":"setHideTitleBar"}],"enabled":true,"id":"%s",'
                '"match":{"all":[{"field":"appId","op":"equals","value":"org.pz.nothing%d"}]},'
                '"name":"probe F625 burst %d","priority":50}') % (rid, i, i)
        method, arg = "addRule", rule
    else:
        method, arg = "removeRule", rid
    reply = bus.call_sync("org.plasmazones", "/PlasmaZones", "org.plasmazones.Rules", method,
                          GLib.Variant("(s)", (arg,)), None, Gio.DBusCallFlags.NONE, 5000, None)
    print(method, i, reply.unpack()[0] if reply.n_children() else "")
EOF
}
COUNTED=0
# measure LABEL: the burst's counts since mmark, checked when its largest gap is under 50 ms
measure() {
    local seg gap
    seg=$(tail -n +"$MMARK" "$MON")
    gap=$(echo "$seg" | grep "member=rulesChanged" | grep -o "time=[0-9.]*" | sed 's/time=//' \
        | awk 'NR > 1 { g = $1 - p; if (g > m) m = g } { p = $1 } END { printf "%d", m * 1000 }')
    echo "  $1: largest gap between rulesChanged signals $gap ms"
    [ "$(echo "$seg" | grep -c "member=rulesChanged")" = 10 ] || stage_fail "$1 did not emit ten rulesChanged signals"
    if [ "$gap" -ge 50 ]; then
        note "$1's signals were 50 ms or more apart, so the debounce may split it; this burst does not count"
        return
    fi
    COUNTED=$((COUNTED + 1))
    check "$1: getAllRules calls" "$(echo "$seg" | grep -c "member=getAllRules")" -eq 1
    check "$1: getPendingRestoreGeometries calls" "$(echo "$seg" | grep -c "member=getPendingRestoreGeometries")" -eq 1
}
mmark "$MON"
burst add | paste -sd' ' | cut -c1-200
sleep 2
measure "burst 1 (ten addRule)"
mmark "$MON"
burst remove | paste -sd' ' | cut -c1-200
sleep 2
measure "burst 2 (ten removeRule)"
[ "$COUNTED" -gt 0 ] || stage_fail "neither burst was tight enough to count"
finish
