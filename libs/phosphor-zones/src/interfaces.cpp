// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorZones/ILayoutManager.h>
#include <PhosphorZones/IZoneDetector.h>

namespace PhosphorZones {

// Out-of-line virtual destructors anchor each interface's vtable in this
// translation unit. Without it every consumer .cpp that includes one of
// these headers would emit its own weak-symbol vtable copy, bloating
// debug info and risking ODR violations across shared-library
// boundaries.
//
// ISettings + IOverlayService destructors live in src/core/interfaces.cpp
// — those interfaces stay in PZ.

ILayoutManager::~ILayoutManager() = default;

IZoneDetector::~IZoneDetector() = default;

} // namespace PhosphorZones
