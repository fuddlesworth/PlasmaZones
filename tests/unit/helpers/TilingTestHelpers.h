// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "autotile/TilingAlgorithm.h"

#include <QRect>
#include <QVector>

namespace PlasmaZones::TestHelpers {

/**
 * @brief Construct TilingParams with named fields (avoids -Wmissing-field-initializers).
 */
inline TilingParams makeParams(int count, const QRect& screen, const TilingState* state, int gap = 0,
                               ::PhosphorLayout::EdgeGaps outerGaps = {}, QVector<QSize> minSizes = {})
{
    TilingParams p;
    p.windowCount = count;
    p.screenGeometry = screen;
    p.state = state;
    p.innerGap = gap;
    p.outerGaps = outerGaps;
    p.minSizes = std::move(minSizes);
    return p;
}

/**
 * @brief Verify zones have the same total area as the screen.
 *
 * This checks area equivalence, not pixel-perfect coverage. It does not
 * guarantee that every pixel of the screen is covered, nor does it detect
 * overlaps — use noOverlaps() for that.
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
