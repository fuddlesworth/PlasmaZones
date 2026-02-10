// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "threecolumnalgorithm.h"

#include <algorithm>

namespace PlasmaZones {

QVector<QRectF> ThreeColumnTilingAlgorithm::generateZones(int windowCount, const TilingParams& params) const
{
    if (windowCount <= 0) {
        return {};
    }

    if (windowCount == 1) {
        return {QRectF(0.0, 0.0, 1.0, 1.0)};
    }

    const int masterCount = std::max(1, std::min(params.masterCount, windowCount));
    const int stackCount = windowCount - masterCount;

    QVector<QRectF> zones;
    zones.reserve(windowCount);

    if (stackCount == 0) {
        // All masters — full width, split into rows
        const qreal masterHeight = 1.0 / masterCount;
        for (int i = 0; i < masterCount; ++i) {
            const qreal y = i * masterHeight;
            const qreal h = (i == masterCount - 1) ? (1.0 - y) : masterHeight;
            zones.append(QRectF(0.0, y, 1.0, h));
        }
        return zones;
    }

    // Distribute stacks: right gets ceiling, left gets floor
    const int rightCount = (stackCount + 1) / 2;
    const int leftCount = stackCount / 2;

    // Compute column geometry based on whether left column is used
    const qreal centerWidth = std::clamp(params.masterRatio, 0.1, 0.9);
    qreal centerX;
    qreal sideWidth;
    qreal rightX;

    if (leftCount > 0) {
        // Three columns: left | center | right
        sideWidth = (1.0 - centerWidth) / 2.0;
        centerX = sideWidth;
        rightX = sideWidth + centerWidth;
    } else {
        // Two columns: center | right (no left column)
        sideWidth = 1.0 - centerWidth;
        centerX = 0.0;
        rightX = centerWidth;
    }

    // Center column (master zones)
    const qreal masterHeight = 1.0 / masterCount;
    for (int i = 0; i < masterCount; ++i) {
        const qreal y = i * masterHeight;
        const qreal h = (i == masterCount - 1) ? (1.0 - y) : masterHeight;
        zones.append(QRectF(centerX, y, centerWidth, h));
    }

    // Right column
    const qreal rightHeight = 1.0 / rightCount;
    for (int i = 0; i < rightCount; ++i) {
        const qreal y = i * rightHeight;
        const qreal h = (i == rightCount - 1) ? (1.0 - y) : rightHeight;
        zones.append(QRectF(rightX, y, sideWidth, h));
    }

    // Left column
    if (leftCount > 0) {
        const qreal leftHeight = 1.0 / leftCount;
        for (int i = 0; i < leftCount; ++i) {
            const qreal y = i * leftHeight;
            const qreal h = (i == leftCount - 1) ? (1.0 - y) : leftHeight;
            zones.append(QRectF(0.0, y, sideWidth, h));
        }
    }

    return zones;
}

} // namespace PlasmaZones
