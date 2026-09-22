#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Screenshot one nested output as a PNG — the harness's eyes for
# headless / AI-driven runs.
#
#   scripts/nested-shell/capture.sh [OutputName] <out.png>
#
# OutputName defaults to Virtual-0 (kwin's first --virtual output; in
# PZ_NESTED_VISIBLE mode list the real names with
# `qdbus6 org.kde.KWin /KWin org.kde.KWin.supportInformation | grep -A2 Screens`
# after sourcing env.sh). Delegates to nested-kwin's capture-output.py
# with the session env loaded.
#
# Unlike the PlasmaZones EFFECT (whose painting ScreenShot2 bypasses,
# hence the geometry-only caveat in the sibling harness), the shell is an
# ordinary layer-shell client on the normal composite path: bars, popouts
# and toasts DO appear in these captures, so they are genuine rendering
# evidence here.
set -eu

REPO="$(cd "$(dirname "$0")/../.." && pwd)"
RUNTIME_DIR="${XDG_RUNTIME_DIR:-/tmp/pz-nested-$(id -u)}"
NEST="${PZ_NESTED_DIR:-$RUNTIME_DIR/pz-nested}"

case $# in
    1) OUTPUT="Virtual-0"; OUT="$1" ;;
    2) OUTPUT="$1"; OUT="$2" ;;
    *) echo "usage: capture.sh [OutputName] <out.png>" >&2; exit 1 ;;
esac

if [ ! -f "$NEST/env.sh" ]; then
    echo "no nested session in $NEST (run scripts/nested-shell/run-shell.sh first)" >&2
    exit 1
fi
# shellcheck disable=SC1091
. "$NEST/env.sh"

exec python3 "$REPO/scripts/nested-kwin/capture-output.py" "$OUTPUT" "$OUT"
