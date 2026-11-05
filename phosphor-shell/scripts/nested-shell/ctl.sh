#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Build-tree phosphorctl aimed at the NESTED shell's socket. The pinned
# --socket is the point: a bare phosphorctl resolves the host session's
# $XDG_RUNTIME_DIR/phosphor.sock, silently driving the wrong shell.
#
#   scripts/nested-shell/ctl.sh call power.toggle
#   scripts/nested-shell/ctl.sh list
set -eu

REPO="$(cd "$(dirname "$0")/../.." && pwd)"
RUNTIME_DIR="${XDG_RUNTIME_DIR:-/tmp/pz-nested-$(id -u)}"
NEST="${PZ_NESTED_DIR:-$RUNTIME_DIR/pz-nested}"

if [ ! -f "$NEST/env.sh" ]; then
    echo "no nested session in $NEST (run scripts/nested-shell/run-shell.sh first)" >&2
    exit 1
fi
# shellcheck disable=SC1091
. "$NEST/env.sh"
BUILD="${PZ_NESTED_BUILD:-build}"

if [ ! -S "$NEST/phosphor.sock" ]; then
    echo "the nested shell's socket $NEST/phosphor.sock is not bound (start it with scripts/nested-shell/shell.sh)" >&2
    exit 1
fi
exec "$REPO/$BUILD/bin/phosphorctl" --socket "$NEST/phosphor.sock" "$@"
