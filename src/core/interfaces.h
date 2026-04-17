// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

// Umbrella header — pulls every PZ service interface in one include.
//
// The four interfaces were split out into per-contract headers so
// zones-related code can depend on just IZoneDetector / ILayoutManager
// without pulling ISettings / IOverlayService along. This header exists
// to keep the ~45 legacy `#include "core/interfaces.h"` sites compiling
// without touching every consumer; new code should prefer the narrower
// header matching the interface it actually uses.

#include <PhosphorZones/ILayoutManager.h>
#include "ioverlayservice.h"
#include "isettings.h"
#include <PhosphorZones/IZoneDetector.h>
