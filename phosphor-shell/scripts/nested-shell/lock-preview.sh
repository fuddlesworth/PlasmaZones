#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: GPL-3.0-or-later
# Run the production lock-screen QML with fake auth, media and power services.
# Requires an existing nested session. The fixture has its own config and IPC
# socket, so the normal shell and its appearance settings remain usable.
#
#   PZ_NESTED_SESSION=review scripts/nested-shell/lock-preview.sh run
#   PZ_NESTED_SESSION=review scripts/nested-shell/lock-preview.sh call preview.state
#   PZ_NESTED_SESSION=review scripts/nested-shell/lock-preview.sh call preview.media --arg enabled=true
#
# `run` stays in the foreground. Stop that process to dismiss the preview.
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
    *) echo "Lock preview requires the nested compositor." >&2; exit 1 ;;
esac
BUILD="${PZ_NESTED_BUILD:-build}"
export PHOSPHOR_SOCKET="$NEST/lock-preview.sock"
if [ "${1:-run}" != run ]; then
    exec "$REPO/$BUILD/bin/phosphorctl" --socket "$PHOSPHOR_SOCKET" "$@"
fi
export XDG_CONFIG_HOME="$NEST/lock-preview/config"
mkdir -p "$XDG_CONFIG_HOME/phosphor-shell"
cp "$REPO/phosphor-shell/scripts/nested-shell/lock-preview.qml" "$XDG_CONFIG_HOME/phosphor-shell/shell.qml"
exec "$REPO/$BUILD/bin/phosphor-shell"
