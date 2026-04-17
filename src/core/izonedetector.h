// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

// Backward-compat shim — actual definition lives in libs/phosphor-zones/.
// Existing `#include "core/izonedetector.h"` consumers keep working; new code
// should prefer `#include <PhosphorZones/IZoneDetector.h>` directly.

#include <PhosphorZones/IZoneDetector.h>
