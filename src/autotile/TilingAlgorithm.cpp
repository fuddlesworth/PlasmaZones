// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "TilingAlgorithm.h"
#include "core/constants.h"
#include <algorithm>

namespace PlasmaZones {

using namespace AutotileDefaults;

TilingAlgorithm::TilingAlgorithm(QObject *parent)
    : QObject(parent)
{
}

int TilingAlgorithm::masterZoneIndex() const
{
    return 0; // Default: first zone is master
}

bool TilingAlgorithm::supportsMasterCount() const
{
    return false;
}

bool TilingAlgorithm::supportsSplitRatio() const
{
    return false;
}

qreal TilingAlgorithm::defaultSplitRatio() const
{
    return DefaultSplitRatio;
}

int TilingAlgorithm::minimumWindows() const
{
    return 1;
}

void TilingAlgorithm::applyGaps(QVector<QRect> &zones, const QRect &screenGeometry, int innerGap, int outerGap)
{
    if (zones.isEmpty()) {
        return;
    }

    // Clamp gap values to reasonable bounds
    innerGap = std::clamp(innerGap, MinGap, MaxGap);
    outerGap = std::clamp(outerGap, MinGap, MaxGap);

    const int screenLeft = screenGeometry.left();
    const int screenTop = screenGeometry.top();
    const int screenRight = screenGeometry.right();
    const int screenBottom = screenGeometry.bottom();

    // Minimum zone size in pixels
    constexpr int minZoneSizePx = 50;

    // For a single zone, only apply outer gaps
    if (zones.size() == 1) {
        QRect &zone = zones[0];
        zone.setLeft(zone.left() + outerGap);
        zone.setTop(zone.top() + outerGap);
        zone.setRight(zone.right() - outerGap);
        zone.setBottom(zone.bottom() - outerGap);

        // Ensure minimum size
        if (zone.width() < minZoneSizePx) {
            zone.setWidth(minZoneSizePx);
        }
        if (zone.height() < minZoneSizePx) {
            zone.setHeight(minZoneSizePx);
        }
        return;
    }

    // For multiple zones, determine adjacency and apply appropriate gaps
    const int halfInner = innerGap / 2;

    // Edge detection threshold (pixels)
    constexpr int edgeThreshold = 5;

    for (QRect &zone : zones) {
        int left = zone.left();
        int top = zone.top();
        int right = zone.right();
        int bottom = zone.bottom();

        // Check if zone is at screen edge (outer gap) or interior (inner gap)
        // Left edge
        if (std::abs(left - screenLeft) <= edgeThreshold) {
            left = screenLeft + outerGap;
        } else {
            left += halfInner;
        }

        // Top edge
        if (std::abs(top - screenTop) <= edgeThreshold) {
            top = screenTop + outerGap;
        } else {
            top += halfInner;
        }

        // Right edge
        if (std::abs(right - screenRight) <= edgeThreshold) {
            right = screenRight - outerGap;
        } else {
            right -= halfInner;
        }

        // Bottom edge
        if (std::abs(bottom - screenBottom) <= edgeThreshold) {
            bottom = screenBottom - outerGap;
        } else {
            bottom -= halfInner;
        }

        // Ensure valid bounds (left < right, top < bottom)
        if (right <= left) {
            right = left + minZoneSizePx;
        }
        if (bottom <= top) {
            bottom = top + minZoneSizePx;
        }

        // Apply adjusted bounds
        zone.setLeft(left);
        zone.setTop(top);
        zone.setRight(right);
        zone.setBottom(bottom);
    }
}

} // namespace PlasmaZones
