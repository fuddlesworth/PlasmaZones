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

int TilingAlgorithm::masterZoneIndex() const noexcept
{
    return -1; // Default: no master concept (subclasses override if they have one)
}

bool TilingAlgorithm::supportsMasterCount() const noexcept
{
    return false;
}

bool TilingAlgorithm::supportsSplitRatio() const noexcept
{
    return false;
}

qreal TilingAlgorithm::defaultSplitRatio() const noexcept
{
    return DefaultSplitRatio;
}

int TilingAlgorithm::minimumWindows() const noexcept
{
    return 1;
}

int TilingAlgorithm::defaultMaxWindows() const noexcept
{
    return DefaultMaxWindows;
}

QVector<int> TilingAlgorithm::distributeEvenly(int total, int count)
{
    QVector<int> sizes;
    if (count <= 0 || total <= 0) {
        return sizes;
    }

    sizes.reserve(count);
    const int base = total / count;
    int remainder = total % count;

    for (int i = 0; i < count; ++i) {
        int size = base;
        // Distribute remainder pixels to first parts
        if (remainder > 0) {
            ++size;
            --remainder;
        }
        sizes.append(size);
    }

    return sizes;
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

    // For a single zone, only apply outer gaps
    if (zones.size() == 1) {
        QRect &zone = zones[0];
        zone.setLeft(zone.left() + outerGap);
        zone.setTop(zone.top() + outerGap);
        zone.setRight(zone.right() - outerGap);
        zone.setBottom(zone.bottom() - outerGap);

        // Ensure minimum size while staying within screen bounds
        if (zone.width() < MinZoneSizePx) {
            const int center = zone.left() + zone.width() / 2;
            zone.setLeft(std::max(screenLeft + outerGap, center - MinZoneSizePx / 2));
            zone.setRight(std::min(screenRight - outerGap, zone.left() + MinZoneSizePx - 1));
        }
        if (zone.height() < MinZoneSizePx) {
            const int center = zone.top() + zone.height() / 2;
            zone.setTop(std::max(screenTop + outerGap, center - MinZoneSizePx / 2));
            zone.setBottom(std::min(screenBottom - outerGap, zone.top() + MinZoneSizePx - 1));
        }
        return;
    }

    // For multiple zones, determine adjacency and apply appropriate gaps
    // For odd inner gaps, use ceiling for left/top offsets and floor for right/bottom
    // This ensures adjacent zones always have exactly innerGap between them
    const int halfInnerFloor = innerGap / 2;
    const int halfInnerCeil = innerGap - halfInnerFloor; // Gets extra pixel if odd

    for (int i = 0; i < zones.size(); ++i) {
        QRect &zone = zones[i];
        const QRect originalZone = zone; // Store original for bounds checking

        int left = zone.left();
        int top = zone.top();
        int right = zone.right();
        int bottom = zone.bottom();

        // Check if zone is at screen edge (outer gap) or interior (inner gap)
        // Interior edges: left/top get ceiling half, right/bottom get floor half
        // This ensures: zone[i].right + halfInnerFloor + halfInnerCeil + zone[i+1].left = innerGap

        // Left edge
        if (std::abs(left - screenLeft) <= GapEdgeThresholdPx) {
            left = screenLeft + outerGap;
        } else {
            left += halfInnerCeil;
        }

        // Top edge
        if (std::abs(top - screenTop) <= GapEdgeThresholdPx) {
            top = screenTop + outerGap;
        } else {
            top += halfInnerCeil;
        }

        // Right edge
        if (std::abs(right - screenRight) <= GapEdgeThresholdPx) {
            right = screenRight - outerGap;
        } else {
            right -= halfInnerFloor;
        }

        // Bottom edge
        if (std::abs(bottom - screenBottom) <= GapEdgeThresholdPx) {
            bottom = screenBottom - outerGap;
        } else {
            bottom -= halfInnerFloor;
        }

        // Ensure valid bounds while preventing overlap with original zone boundaries
        // This prevents zones from expanding beyond their allocated space
        if (right <= left) {
            // Zone too narrow - center the minimum size within original bounds
            const int center = originalZone.left() + originalZone.width() / 2;
            left = std::max(originalZone.left(), center - MinZoneSizePx / 2);
            right = std::min(originalZone.right(), left + MinZoneSizePx - 1);
        }
        if (bottom <= top) {
            // Zone too short - center the minimum size within original bounds
            const int center = originalZone.top() + originalZone.height() / 2;
            top = std::max(originalZone.top(), center - MinZoneSizePx / 2);
            bottom = std::min(originalZone.bottom(), top + MinZoneSizePx - 1);
        }

        // Apply adjusted bounds
        zone.setLeft(left);
        zone.setTop(top);
        zone.setRight(right);
        zone.setBottom(bottom);
    }
}

} // namespace PlasmaZones