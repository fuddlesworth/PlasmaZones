// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ioverlayservice.h"
#include "isettings.h"

namespace PlasmaZones {

// Out-of-line virtual destructors anchor each interface's vtable in this
// translation unit. Prevents ODR violations across shared-library
// boundaries.
//
// ILayoutManager + IZoneDetector destructors live in
// libs/phosphor-zones/src/interfaces.cpp — those interfaces moved with
// the zones extraction.

ISettings::~ISettings() = default;

IOverlayService::~IOverlayService() = default;

} // namespace PlasmaZones
