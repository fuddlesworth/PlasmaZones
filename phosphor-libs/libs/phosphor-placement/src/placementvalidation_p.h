// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Private (non-installed) header — Zone/Layout validation helpers shared
// between lifecycle.cpp (onLayoutChanged stale-assignment cleanup) and
// virtualscreenmigration.cpp (the VS migration cluster). Defined `inline` (not
// `static`) so the half that uses only one of the pair raises no
// -Wunused-function.

#pragma once

#include "placementutils.h"

#include <PhosphorZones/Layout.h>

#include <QStringList>

namespace PhosphorPlacement {

/// Returns true if ANY of the given zone IDs exists in the layout.
inline bool anyZoneExistsInLayout(const QStringList& zoneIds, PhosphorZones::Layout* layout)
{
    if (!layout)
        return false;
    for (const QString& zid : zoneIds) {
        auto uuid = parseUuid(zid);
        if (uuid && layout->zoneById(*uuid))
            return true;
    }
    return false;
}

/// Returns true if ALL of the given zone IDs exist in the layout.
inline bool allZonesExistInLayout(const QStringList& zoneIds, PhosphorZones::Layout* layout)
{
    if (!layout)
        return false;
    for (const QString& zid : zoneIds) {
        auto uuid = parseUuid(zid);
        if (!uuid || !layout->zoneById(*uuid))
            return false;
    }
    return true;
}

} // namespace PhosphorPlacement
