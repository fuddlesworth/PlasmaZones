#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: GPL-3.0-or-later
#
# fu-dbusctx-spoof-refused: a snap-assist thumbnail push from a process that is not the compositor
# is refused with an AccessDenied error. Runner: 1 output, no flags.
# Staging: none beyond the session. gdbus, run from this probe's shell on the nested bus, calls
#   Overlay.setSnapAssistThumbnail with a 2x2 image, twice.
# Asserts: both calls answer org.freedesktop.DBus.Error.AccessDenied "only the compositor may push
#   snap assist thumbnails"; the daemon warns once, "trustIfCompositor: rejecting <name> pid= <gdbus>
#   parent= <shell> - the compositor is pid <kwin> (further refusals log at debug)", and the second
#   refusal is a debug line only.
# On main (8ca183308): the call answers "(true,)" or "(false,)" with no error.
set -u
NEST="$1"
LOG="$2"
. "$(dirname "$0")/../lib.sh"
kwjs_ok
KP=$(kwinpid)
[ -n "$KP" ] || stage_fail "no nested kwin_wayland pid for socket $WAYLAND_DISPLAY"
push() {
    gdbus call --session --timeout 10 --dest org.plasmazones --object-path /PlasmaZones \
        --method org.plasmazones.Overlay.setSnapAssistThumbnail "{00000000-0000-0000-0000-000000000001}" 2 2 \
        "[byte 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0]" 2>&1 | tr '\n' ' '
}
dmark
R1=$(push)
echo "first push: $R1"
check "first push: the error" "$R1" '~' "org\.freedesktop\.DBus\.Error\.AccessDenied"
check "first push: the message" "$R1" '~' "only the compositor may push snap assist thumbnails"
sleep 0.5
R2=$(push)
echo "second push: $R2"
check "second push: the error" "$R2" '~' "org\.freedesktop\.DBus\.Error\.AccessDenied"
sleep 0.5
dlog "trustIfCompositor: rejecting.*" | sed 's/^/    /'
check "refusal warnings naming the nested kwin as the compositor" \
    "$(dlog "trustIfCompositor: rejecting [^ ]+ pid= [0-9]+ parent= [0-9]+ - the compositor is pid $KP \(further refusals log at debug\)" | wc -l)" -eq 1
check "refusal lines over both pushes" "$(dlog "trustIfCompositor: rejecting" | wc -l)" -ge 2
finish
