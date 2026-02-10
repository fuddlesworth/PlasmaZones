// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "bspalgorithm.h"

#include <algorithm>
#include <QQueue>

namespace PlasmaZones {

QVector<QRectF> BSPTilingAlgorithm::generateZones(int windowCount, const TilingParams& params) const
{
    if (windowCount <= 0) {
        return {};
    }

    if (windowCount == 1) {
        return {QRectF(0.0, 0.0, 1.0, 1.0)};
    }

    const qreal masterRatio = std::clamp(params.masterRatio, 0.1, 0.9);

    struct Region {
        QRectF rect;
        int depth;
        int count;
    };

    QVector<QRectF> zones;
    zones.reserve(windowCount);

    QQueue<Region> queue;
    queue.enqueue({QRectF(0.0, 0.0, 1.0, 1.0), 0, windowCount});

    while (!queue.isEmpty()) {
        const Region region = queue.dequeue();

        if (region.count == 1) {
            zones.append(region.rect);
            continue;
        }

        const int leftCount = region.count / 2;
        const int rightCount = region.count - leftCount;

        // Depth 0 uses masterRatio, deeper levels use 50/50
        const qreal ratio = (region.depth == 0) ? masterRatio : 0.5;

        // Even depth = vertical split, odd depth = horizontal split
        if (region.depth % 2 == 0) {
            // Vertical split
            const qreal splitX = region.rect.x() + region.rect.width() * ratio;
            const qreal leftWidth = splitX - region.rect.x();
            const qreal rightWidth = region.rect.right() - splitX;

            queue.enqueue({QRectF(region.rect.x(), region.rect.y(), leftWidth, region.rect.height()),
                           region.depth + 1, leftCount});
            queue.enqueue({QRectF(splitX, region.rect.y(), rightWidth, region.rect.height()),
                           region.depth + 1, rightCount});
        } else {
            // Horizontal split
            const qreal splitY = region.rect.y() + region.rect.height() * ratio;
            const qreal topHeight = splitY - region.rect.y();
            const qreal bottomHeight = region.rect.bottom() - splitY;

            queue.enqueue({QRectF(region.rect.x(), region.rect.y(), region.rect.width(), topHeight),
                           region.depth + 1, leftCount});
            queue.enqueue({QRectF(region.rect.x(), splitY, region.rect.width(), bottomHeight),
                           region.depth + 1, rightCount});
        }
    }

    return zones;
}

} // namespace PlasmaZones
