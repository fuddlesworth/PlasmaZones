// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "plasmazones_export.h"
#include "utils.h"

#include <QRectF>
#include <QString>

#include <cmath>
#include <limits>

namespace PlasmaZones::SpatialAdjacency {

/**
 * Find the index of the rect in @p candidates that is most "adjacent" to
 * @p current in the given @p direction. Used for zone-to-zone directional
 * navigation and (by the same rules) virtual-screen-to-virtual-screen
 * directional navigation.
 *
 * A rect qualifies as "in direction" if its centre is past the current centre
 * on the direction axis. Among qualifying rects, the one with the minimum
 * weighted distance is returned, where the perpendicular-axis offset is
 * weighted 2x to prefer same-row / same-column neighbours on grid layouts.
 *
 * @return index into @p candidates, or -1 if no rect qualifies. Rects that
 *         compare equal to @p current (same centre) are skipped.
 */
inline int findAdjacentRect(const QRectF& current, const QList<QRectF>& candidates, const QString& direction)
{
    const QPointF currentCenter = current.center();

    int bestIndex = -1;
    qreal bestDistance = std::numeric_limits<qreal>::max();

    for (int i = 0; i < candidates.size(); ++i) {
        const QRectF& candidate = candidates.at(i);
        const QPointF candidateCenter = candidate.center();

        if (candidateCenter == currentCenter) {
            continue;
        }

        bool valid = false;
        qreal distance = 0;

        if (direction == Utils::Direction::Left) {
            if (candidateCenter.x() < currentCenter.x()) {
                valid = true;
                distance = currentCenter.x() - candidateCenter.x();
                distance += std::abs(candidateCenter.y() - currentCenter.y()) * 2;
            }
        } else if (direction == Utils::Direction::Right) {
            if (candidateCenter.x() > currentCenter.x()) {
                valid = true;
                distance = candidateCenter.x() - currentCenter.x();
                distance += std::abs(candidateCenter.y() - currentCenter.y()) * 2;
            }
        } else if (direction == Utils::Direction::Up) {
            if (candidateCenter.y() < currentCenter.y()) {
                valid = true;
                distance = currentCenter.y() - candidateCenter.y();
                distance += std::abs(candidateCenter.x() - currentCenter.x()) * 2;
            }
        } else if (direction == Utils::Direction::Down) {
            if (candidateCenter.y() > currentCenter.y()) {
                valid = true;
                distance = candidateCenter.y() - currentCenter.y();
                distance += std::abs(candidateCenter.x() - currentCenter.x()) * 2;
            }
        }

        if (valid && distance < bestDistance) {
            bestDistance = distance;
            bestIndex = i;
        }
    }

    return bestIndex;
}

} // namespace PlasmaZones::SpatialAdjacency
