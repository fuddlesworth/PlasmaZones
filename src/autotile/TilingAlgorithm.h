// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

// Backward-compat shim — actual definition lives in libs/phosphor-tiles/.
// Existing `#include "autotile/TilingAlgorithm.h"` consumers keep working;
// new code should prefer `#include <PhosphorTiles/TilingAlgorithm.h>`
// directly.

#include <PhosphorTiles/TilingAlgorithm.h>
