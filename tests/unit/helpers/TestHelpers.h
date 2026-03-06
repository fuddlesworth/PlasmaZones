// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QVariantMap>
#include <QString>
#include <QLatin1String>

#include "core/constants.h"

namespace PlasmaZones {
namespace TestHelpers {

/**
 * @brief Create a zone QVariantMap with pixel coordinates and highlight state.
 *
 * Uses JsonKeys constants from core/constants.h for all key names.
 * Shared across test_zone_shader_item.cpp and test_overlay_helpers.cpp.
 *
 * @param id Zone identifier (optional, omitted from map if empty)
 * @param x X coordinate in pixels
 * @param y Y coordinate in pixels
 * @param w Width in pixels
 * @param h Height in pixels
 * @param zoneNumber Zone number label (default 0)
 * @param highlighted Whether the zone is highlighted (default false)
 * @return QVariantMap representing the zone
 */
inline QVariantMap makeZone(const QString& id, float x, float y, float w, float h, int zoneNumber = 0,
                            bool highlighted = false)
{
    QVariantMap z;
    if (!id.isEmpty()) {
        z.insert(JsonKeys::Id, id);
    }
    z.insert(JsonKeys::X, x);
    z.insert(JsonKeys::Y, y);
    z.insert(JsonKeys::Width, w);
    z.insert(JsonKeys::Height, h);
    z.insert(JsonKeys::ZoneNumber, zoneNumber);
    z.insert(JsonKeys::IsHighlighted, highlighted);
    return z;
}

/**
 * @brief Overload without id parameter for shader tests that don't need zone IDs.
 */
inline QVariantMap makeZone(float x, float y, float w, float h, int zoneNumber = 0, bool highlighted = false)
{
    return makeZone(QString(), x, y, w, h, zoneNumber, highlighted);
}

} // namespace TestHelpers
} // namespace PlasmaZones
