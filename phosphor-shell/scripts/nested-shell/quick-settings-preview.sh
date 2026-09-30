#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: GPL-3.0-or-later
# Exercise production detail panels with isolated device fixtures.
# Start a nested shell first, then run this script; call preview.state to inspect.
# Use run-status [settings|overflow|icon-id] for the hardware-free status UI.
# preview.statusState accepts everyday, meeting, travel, desktop, all, empty,
# unavailable, pending and error. Settings persist inside the preview sandbox.
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
    *) echo "Quick-settings preview requires the nested compositor." >&2; exit 1 ;;
esac
BUILD="${PZ_NESTED_BUILD:-build}"
export PHOSPHOR_SOCKET="$NEST/quick-settings-preview.sock"
case "${1:-run}" in
    run) STATUS_PAGE= ;;
    run-status)
        STATUS_PAGE="${2:-settings}"
        case "$STATUS_PAGE" in
            settings|overflow|wifi|audio|bluetooth|battery|microphone|nightlight|focus|airplane|power) ;;
            *) echo "Unknown status page: $STATUS_PAGE" >&2; exit 1 ;;
        esac
        ;;
    *) exec "$REPO/$BUILD/bin/phosphorctl" --socket "$PHOSPHOR_SOCKET" "$@" ;;
esac
export XDG_CONFIG_HOME="$NEST/quick-settings-preview/config"
mkdir -p "$XDG_CONFIG_HOME/phosphor-shell"
cp "$REPO/phosphor-shell/scripts/nested-shell/quick-settings-preview.qml" "$XDG_CONFIG_HOME/phosphor-shell/shell.qml"
cp "$REPO/phosphor-shell/scripts/nested-shell/StatusIconsPreviewController.qml" "$XDG_CONFIG_HOME/phosphor-shell/StatusIconsPreviewController.qml"
cp "$REPO/phosphor-shell/shell/wallpapers/spectrum.png" "$XDG_CONFIG_HOME/phosphor-shell/fixture-spectrum.png"
cp "$REPO/phosphor-shell/shell/wallpapers/ember.png" "$XDG_CONFIG_HOME/phosphor-shell/fixture-ember.png"
if [ -n "$STATUS_PAGE" ]; then
    sed -i -e 's/property string view: "wifi"/property string view: "status-icons"/' \
        -e 's/property bool statusVisible: false/property bool statusVisible: true/' \
        -e "s/property string statusPage: \"settings\"/property string statusPage: \"$STATUS_PAGE\"/" \
        "$XDG_CONFIG_HOME/phosphor-shell/shell.qml"
fi
# Enable audio writes only against an explicitly supplied nested fixture.
case "${PIPEWIRE_RUNTIME_DIR:-}" in
    "$NEST"/*)
        sed -i 's/property bool audioEnabled: false/property bool audioEnabled: true/' "$XDG_CONFIG_HOME/phosphor-shell/shell.qml"
        ;;
esac
exec "$REPO/$BUILD/bin/phosphor-shell"
