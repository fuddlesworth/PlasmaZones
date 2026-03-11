// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QRect>
#include <QVector>

namespace PlasmaZones::TestHelpers {

/**
 * @brief Verify zones fill the screen exactly (total area equals screen area).
 *
 * Note: This does not catch overlaps, only ensures total coverage.
 */
inline bool zonesFillScreen(const QVector<QRect>& zones, const QRect& screen)
{
    int totalArea = 0;
    for (const QRect& zone : zones) {
        totalArea += zone.width() * zone.height();
    }
    return totalArea == screen.width() * screen.height();
}

/**
 * @brief Verify no two zones overlap.
 */
inline bool noOverlaps(const QVector<QRect>& zones)
{
    for (int i = 0; i < zones.size(); ++i) {
        for (int j = i + 1; j < zones.size(); ++j) {
            if (zones[i].intersects(zones[j])) {
                return false;
            }
        }
    }
    return true;
}

/**
 * @brief Verify all zones are within the given screen bounds.
 */
inline bool allWithinBounds(const QVector<QRect>& zones, const QRect& screen)
{
    for (const QRect& zone : zones) {
        if (!screen.contains(zone)) {
            return false;
        }
    }
    return true;
}

} // namespace PlasmaZones::TestHelpers
