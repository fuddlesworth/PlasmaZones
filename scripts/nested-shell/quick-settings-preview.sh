#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: GPL-3.0-or-later
# Exercise production detail panels with isolated device fixtures.
# Start a nested shell first, then run this script; call preview.state to inspect.
set -eu
REPO="$(cd "$(dirname "$0")/../.." && pwd)"
NEST="${PZ_NESTED_DIR:-${XDG_RUNTIME_DIR:-/tmp/pz-nested-$(id -u)}/pz-nested${PZ_NESTED_SESSION:+-$PZ_NESTED_SESSION}}"
if [ ! -f "$NEST/env.sh" ]; then
    echo "Start a nested session with run-shell.sh first." >&2
    exit 1
fi
# shellcheck disable=SC1091
. "$NEST/env.sh"
case "${WAYLAND_DISPLAY:-}" in
    pznested*) ;;
    *) echo "Quick-settings preview requires the nested compositor." >&2; exit 1 ;;
esac
BUILD="${PZ_NESTED_BUILD:-build}"
export PHOSPHOR_SOCKET="$NEST/quick-settings-preview.sock"
if [ "${1:-run}" != run ]; then
    exec "$REPO/$BUILD/bin/phosphorctl" --socket "$PHOSPHOR_SOCKET" "$@"
fi
export XDG_CONFIG_HOME="$NEST/quick-settings-preview/config"
mkdir -p "$XDG_CONFIG_HOME/phosphor-shell"
cp "$REPO/scripts/nested-shell/quick-settings-preview.qml" "$XDG_CONFIG_HOME/phosphor-shell/shell.qml"
exec "$REPO/$BUILD/bin/phosphor-shell"
