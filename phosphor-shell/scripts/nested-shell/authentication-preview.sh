#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: GPL-3.0-or-later
# Native production PolkitSurface with a preview-only agent. No Polkit/PAM
# registration, host account changes, or real authorization is possible.
#
# PZ_NESTED_SESSION=review phosphor-shell/scripts/nested-shell/authentication-preview.sh run
# PZ_NESTED_SESSION=review phosphor-shell/scripts/nested-shell/authentication-preview.sh run --example accounts --state error
# PZ_NESTED_SESSION=review phosphor-shell/scripts/nested-shell/authentication-preview.sh call preview.show --arg example=code --arg state=ready
# PZ_NESTED_SESSION=review phosphor-shell/scripts/nested-shell/authentication-preview.sh call preview.phase --arg name=unavailable
# PZ_NESTED_SESSION=review phosphor-shell/scripts/nested-shell/authentication-preview.sh call preview.state
# PZ_NESTED_SESSION=review phosphor-shell/scripts/nested-shell/authentication-preview.sh call preview.quit
#
# Submit "demo" (or "123456" for the code example). Other input exercises the
# retry state. Captures and input use the existing nested compositor tooling.
set -eu
REPO="$(cd "$(dirname "$0")/../../.." && pwd)"
NEST="${PZ_NESTED_DIR:-${XDG_RUNTIME_DIR:-/tmp/pz-nested-$(id -u)}/pz-nested${PZ_NESTED_SESSION:+-$PZ_NESTED_SESSION}}"
if [ ! -f "$NEST/env.sh" ]; then
    echo "Start a nested session with run-shell.sh first." >&2
    exit 1
fi
# shellcheck disable=SC1091
. "$NEST/env.sh"
case "${WAYLAND_DISPLAY:-}" in
    pznested*) ;;
    *) echo "Authentication preview requires the nested compositor." >&2; exit 1 ;;
esac
BUILD="${PZ_NESTED_BUILD:-build}"
case "$BUILD" in
    /*) ;;
    *) BUILD="$REPO/$BUILD" ;;
esac
export PHOSPHOR_SOCKET="$NEST/authentication-preview.sock"
if [ "${1:-run}" != run ]; then
    exec "$BUILD/bin/phosphorctl" --socket "$PHOSPHOR_SOCKET" "$@"
fi
if [ "$#" -gt 0 ]; then
    shift
fi
export XDG_CONFIG_HOME="$NEST/authentication-preview/config"
export XDG_DATA_HOME="$NEST/authentication-preview/data"
export XDG_CACHE_HOME="$NEST/authentication-preview/cache"
mkdir -p "$XDG_CONFIG_HOME" "$XDG_DATA_HOME" "$XDG_CACHE_HOME"
exec "$BUILD/bin/phosphor-authentication-preview" --socket "$PHOSPHOR_SOCKET" "$@"
