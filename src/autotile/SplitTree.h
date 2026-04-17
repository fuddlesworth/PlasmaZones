// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

// Backward-compat shim — actual definition lives in libs/phosphor-tiles/.
// Existing `#include "autotile/SplitTree.h"` consumers keep working;
// new code should prefer `#include <PhosphorTiles/SplitTree.h>` directly.

#include <PhosphorTiles/SplitTree.h>
