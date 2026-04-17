// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

// Backward-compat shim — actual definition lives in libs/phosphor-zones/.
// Existing `#include "core/zone.h"` consumers keep working; new code
// should prefer `#include <PhosphorZones/Zone.h>` directly.

#include <PhosphorZones/Zone.h>
