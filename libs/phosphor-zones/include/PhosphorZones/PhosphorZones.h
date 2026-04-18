// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

/// Umbrella include for PhosphorZones. Consumers may include individual
/// headers for tighter dependencies. Consumers that only need a read-only
/// view of the layout set should prefer @c ILayoutCatalog (declared in
/// ILayoutManager.h) over the full @c ILayoutManager surface.

#include <PhosphorZones/ILayoutManager.h>
#include <PhosphorZones/IZoneDetector.h>
#include <PhosphorZones/Layout.h>
#include <PhosphorZones/LayoutUtils.h>
#include <PhosphorZones/Zone.h>
#include <PhosphorZones/ZoneDetector.h>
#include <PhosphorZones/ZoneHighlighter.h>
#include <PhosphorZones/ZonesLayoutSource.h>
