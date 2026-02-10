// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "masterstackalgorithm.h"

#include <algorithm>

namespace PlasmaZones {

QVector<QRectF> MasterStackTilingAlgorithm::generateZones(int windowCount, const TilingParams& params) const
{
    if (windowCount <= 0) {
        return {};
    }

    if (windowCount == 1) {
        return {QRectF(0.0, 0.0, 1.0, 1.0)};
    }

    const int masterCount = std::max(1, std::min(params.masterCount, windowCount));
    const qreal masterRatio = std::clamp(params.masterRatio, 0.1, 0.9);
    const int stackCount = windowCount - masterCount;

    QVector<QRectF> zones;
    zones.reserve(windowCount);

    if (stackCount == 0) {
        // All windows in master area — full width, split vertically
        const qreal height = 1.0 / masterCount;
        for (int i = 0; i < masterCount; ++i) {
            const qreal y = i * height;
            const qreal h = (i == masterCount - 1) ? (1.0 - y) : height;
            zones.append(QRectF(0.0, y, 1.0, h));
        }
    } else {
        // Master area on left, stack on right
        const qreal masterWidth = masterRatio;
        const qreal stackWidth = 1.0 - masterWidth;
        const qreal masterHeight = 1.0 / masterCount;
        const qreal stackHeight = 1.0 / stackCount;

        // Master zones
        for (int i = 0; i < masterCount; ++i) {
            const qreal y = i * masterHeight;
            const qreal h = (i == masterCount - 1) ? (1.0 - y) : masterHeight;
            zones.append(QRectF(0.0, y, masterWidth, h));
        }

        // Stack zones
        for (int i = 0; i < stackCount; ++i) {
            const qreal y = i * stackHeight;
            const qreal h = (i == stackCount - 1) ? (1.0 - y) : stackHeight;
            zones.append(QRectF(masterWidth, y, stackWidth, h));
        }
    }

    return zones;
}

} // namespace PlasmaZones
