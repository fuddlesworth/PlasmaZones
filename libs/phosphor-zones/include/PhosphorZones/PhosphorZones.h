// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

/// Umbrella include for PhosphorZones. Consumers may include individual
/// headers for tighter dependencies. Consumers that only need to
/// enumerate / look up layouts should prefer @c ILayoutRegistry (in
/// ILayoutRegistry.h) over the full @c ILayoutManager surface.

#include <PhosphorZones/ILayoutManager.h>
#include <PhosphorZones/IZoneDetector.h>
#include <PhosphorZones/Layout.h>
#include <PhosphorZones/LayoutUtils.h>
#include <PhosphorZones/Zone.h>
#include <PhosphorZones/ZoneDetector.h>
#include <PhosphorZones/ZoneHighlighter.h>
#include <PhosphorZones/ZonesLayoutSource.h>
