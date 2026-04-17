// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

// Backward-compat shim — actual definition lives in libs/phosphor-zones/.
// Existing `#include "core/ilayoutmanager.h"` consumers keep working; new code
// should prefer `#include <PhosphorZones/ILayoutManager.h>` directly.

#include <PhosphorZones/ILayoutManager.h>
