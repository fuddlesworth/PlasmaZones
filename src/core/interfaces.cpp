// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "interfaces.h"

namespace PlasmaZones {

// Key functions for interface classes to anchor vtables to this translation unit
// This prevents ODR violations when interfaces are used across shared library boundaries

ISettings::~ISettings() = default;

ILayoutManager::~ILayoutManager() = default;

IZoneDetector::~IZoneDetector() = default;

IOverlayService::~IOverlayService() = default;

} // namespace PlasmaZones
