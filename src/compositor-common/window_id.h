// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

// Forwarding shim for the KWin effect's include-path. The canonical header
// is <PhosphorIdentity/WindowId.h>; this file only exists because the KWin
// effect build (kwin-effect/) cannot add the PhosphorIdentity target to its
// include search path without dragging in the rest of the core/daemon
// dependency graph. New callers should use the canonical path.

#include <PhosphorIdentity/WindowId.h>
