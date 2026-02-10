// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "fibonaccialgorithm.h"

#include <algorithm>

namespace PlasmaZones {

QVector<QRectF> FibonacciTilingAlgorithm::generateZones(int windowCount, const TilingParams& params) const
{
    if (windowCount <= 0) {
        return {};
    }

    if (windowCount == 1) {
        return {QRectF(0.0, 0.0, 1.0, 1.0)};
    }

    QVector<QRectF> zones;
    zones.reserve(windowCount);

    // First split: vertical, zone 1 = left masterRatio, remaining = right
    QRectF remaining(0.0, 0.0, 1.0, 1.0);

    const qreal masterRatio = std::clamp(params.masterRatio, 0.1, 0.9);
    const qreal masterWidth = remaining.width() * masterRatio;
    zones.append(QRectF(remaining.x(), remaining.y(), masterWidth, remaining.height()));
    remaining = QRectF(remaining.x() + masterWidth, remaining.y(),
                       remaining.width() - masterWidth, remaining.height());

    // Each subsequent split alternates H/V, peeling ONE window
    for (int i = 1; i < windowCount - 1; ++i) {
        if (i % 2 == 1) {
            // Even index in 0-based from second window → horizontal split
            // Zone takes top half, remaining = bottom
            const qreal splitHeight = remaining.height() * 0.5;
            zones.append(QRectF(remaining.x(), remaining.y(),
                                remaining.width(), splitHeight));
            remaining = QRectF(remaining.x(), remaining.y() + splitHeight,
                               remaining.width(), remaining.height() - splitHeight);
        } else {
            // Odd index → vertical split
            // Zone takes left half, remaining = right
            const qreal splitWidth = remaining.width() * 0.5;
            zones.append(QRectF(remaining.x(), remaining.y(),
                                splitWidth, remaining.height()));
            remaining = QRectF(remaining.x() + splitWidth, remaining.y(),
                               remaining.width() - splitWidth, remaining.height());
        }
    }

    // Last window = remaining area
    zones.append(remaining);

    return zones;
}

} // namespace PlasmaZones
