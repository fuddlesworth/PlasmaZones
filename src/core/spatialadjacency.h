// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "plasmazones_export.h"

#include <PhosphorGeometry/DirectionalNeighbor.h>

#include <QList>
#include <QRectF>
#include <QString>

namespace PlasmaZones::SpatialAdjacency {

/**
 * Find the index of the rect in @p candidates that is most "adjacent" to
 * @p current in the given @p direction. Used for zone-to-zone directional
 * navigation and (by the same rules) virtual-screen-to-virtual-screen
 * directional navigation.
 *
 * Thin wrapper over the shared, license-clean primitive
 * `PhosphorGeometry::directionalNeighbor`, so zone navigation, autotile window
 * navigation, and screen navigation all rank candidates identically: an
 * in-direction filter, then a perpendicular-overlap preference (genuinely
 * side-by-side beats diagonal), then nearest edge gap, then a deterministic
 * centre tie-break.
 *
 * @param direction one of `PhosphorScreens::Direction::{Left,Right,Up,Down}`
 *        (the lower-case tokens "left"/"right"/"up"/"down"). Any other token
 *        yields -1 (no match) rather than a defaulted direction.
 *
 * @note Coordinate-system agnostic — @p current and all @p candidates must
 *       share one space (absolute-pixel zone rects, or unit-square [0,1]
 *       virtual-screen regions).
 *
 * @return index into @p candidates, or -1 if no rect qualifies. Rects sharing
 *         @p current's centre are skipped.
 */
inline int findAdjacentRect(const QRectF& current, const QList<QRectF>& candidates, const QString& direction)
{
    const auto parsed = PhosphorGeometry::directionFromString(direction);
    if (!parsed.has_value()) {
        return -1;
    }
    return PhosphorGeometry::directionalNeighbor(current, candidates, *parsed);
}

} // namespace PlasmaZones::SpatialAdjacency
