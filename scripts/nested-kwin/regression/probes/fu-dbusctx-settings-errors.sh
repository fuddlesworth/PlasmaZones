#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: GPL-3.0-or-later
#
# fu-dbusctx-settings-errors: the Settings adaptor answers a bad request with an InvalidArgs error
# at once instead of leaving the caller waiting. Runner: 1 output, no flags.
# Staging: none beyond the session. gdbus calls, each with a 5 s timeout and timed.
# Asserts: getSetting noSuchSetting answers org.freedesktop.DBus.Error.InvalidArgs "Unknown setting
#   key: noSuchSetting" in under 3 s; getSetting '' answers InvalidArgs "Empty setting key";
#   setPerScreenSetting Virtual-0 bogus masterCount <int32 2> answers InvalidArgs "unknown category
#   bogus"; getSetting borderWidth answers a value.
# On main (8ca183308): the error reply never reaches the caller, so each bad request runs into the
#   5 s timeout ("Timeout was reached") and the daemon logs Qt's "could not send reply message".
set -u
NEST="$1"
LOG="$2"
. "$(dirname "$0")/../lib.sh"
kwjs_ok
# call METHOD ARG...: a Settings call with a 5 s timeout; sets REPLY (one line) and MS (elapsed)
call() {
    local t0 t1
    local method="$1"
    shift
    t0=$(date +%s%N)
    REPLY=$(gdbus call --session --timeout 5 --dest org.plasmazones --object-path /PlasmaZones \
        --method "org.plasmazones.Settings.$method" "$@" 2>&1 | tr '\n' ' ')
    t1=$(date +%s%N)
    MS=$(((t1 - t0) / 1000000))
    echo "  $method $*: $REPLY (${MS} ms)"
}
call getSetting noSuchSetting
check "getSetting noSuchSetting: the error" "$REPLY" '~' "org\.freedesktop\.DBus\.Error\.InvalidArgs"
check "getSetting noSuchSetting: the message" "$REPLY" '~' "Unknown setting key: noSuchSetting"
check "getSetting noSuchSetting: answered within 3 s (ms)" "$MS" -lt 3000
call getSetting "''"
check "getSetting '': the error" "$REPLY" '~' "org\.freedesktop\.DBus\.Error\.InvalidArgs"
check "getSetting '': the message" "$REPLY" '~' "Empty setting key"
call setPerScreenSetting Virtual-0 bogus masterCount "<int32 2>"
check "setPerScreenSetting bogus category: the error" "$REPLY" '~' "org\.freedesktop\.DBus\.Error\.InvalidArgs"
check "setPerScreenSetting bogus category: the message" "$REPLY" '~' "unknown category bogus"
call getSetting borderWidth
check "getSetting borderWidth: a value, no error" "$REPLY" '~' "^\(<"
finish
