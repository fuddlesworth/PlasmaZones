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

QRect TilingAlgorithm::innerRect(const QRect &screenGeometry, int outerGap)
{
    outerGap = std::max(0, outerGap);
    const int w = std::max(1, screenGeometry.width() - 2 * outerGap);
    const int h = std::max(1, screenGeometry.height() - 2 * outerGap);
    // When outerGap exceeds half the screen dimension, center the result
    // to avoid placing the rect off-screen
    const int x = screenGeometry.left() + (screenGeometry.width() - w) / 2;
    const int y = screenGeometry.top() + (screenGeometry.height() - h) / 2;
    return QRect(x, y, w, h);
}

QRect TilingAlgorithm::innerRect(const QRect &screenGeometry, const EdgeGaps &gaps)
{
    const int l = std::max(0, gaps.left);
    const int r = std::max(0, gaps.right);
    const int t = std::max(0, gaps.top);
    const int b = std::max(0, gaps.bottom);
    const int w = std::max(1, screenGeometry.width() - l - r);
    const int h = std::max(1, screenGeometry.height() - t - b);
    // When gaps exceed screen dimension, center the result to avoid placing
    // the rect off-screen (same behavior as the uniform overload)
    const int x = (l + r >= screenGeometry.width())
        ? screenGeometry.left() + (screenGeometry.width() - w) / 2
        : screenGeometry.left() + l;
    const int y = (t + b >= screenGeometry.height())
        ? screenGeometry.top() + (screenGeometry.height() - h) / 2
        : screenGeometry.top() + t;
    return QRect(x, y, w, h);
}

QVector<int> TilingAlgorithm::distributeWithGaps(int total, int count, int gap)
{
    if (count <= 0 || total <= 0) {
        return {};
    }
    if (count == 1) {
        return {total};
    }
    // Deduct space used by gaps between items
    const int totalGaps = (count - 1) * gap;
    const int available = std::max(count, total - totalGaps); // at least 1px per item
    return distributeEvenly(available, count);
}

QVector<int> TilingAlgorithm::distributeWithMinSizes(int total, int count, int gap,
                                                     const QVector<int> &minDims)
{
    if (count <= 0 || total <= 0) {
        return {};
    }
    if (count == 1) {
        return {total};
    }

    // Deduct space used by gaps between items
    const int totalGaps = (count - 1) * gap;
    const int available = std::max(count, total - totalGaps); // at least 1px per item

    // If no constraints provided, fall back to even distribution
    if (minDims.isEmpty()) {
        return distributeEvenly(available, count);
    }

    // Build effective minimum per item (at least 1px)
    QVector<int> mins(count);
    int totalMin = 0;
    for (int i = 0; i < count; ++i) {
        mins[i] = (i < minDims.size() && minDims[i] > 0) ? minDims[i] : 1;
        totalMin += mins[i];
    }

    QVector<int> sizes(count);

    if (totalMin >= available) {
        // Unsatisfiable: distribute proportionally by minimum weight
        int remaining = available;
        int remainingMin = totalMin;
        for (int i = 0; i < count; ++i) {
            int allocated;
            if (remainingMin > 0) {
                allocated = static_cast<int>(
                    static_cast<qint64>(remaining) * mins[i] / remainingMin);
            } else {
                allocated = remaining / std::max(1, count - i);
            }
            allocated = std::max(1, std::min(allocated, remaining));
            sizes[i] = allocated;
            remaining -= allocated;
            remainingMin -= mins[i];
        }
    } else {
        // Satisfiable: give each its minimum, distribute surplus evenly
        const int surplus = available - totalMin;
        const int base = surplus / count;
        int remainder = surplus % count;

        for (int i = 0; i < count; ++i) {
            sizes[i] = mins[i] + base;
            if (remainder > 0) {
                ++sizes[i];
                --remainder;
            }
        }
    }

    return sizes;
}

int TilingAlgorithm::minWidthAt(const QVector<QSize> &minSizes, int index)
{
    return (index >= 0 && index < minSizes.size()) ? std::max(0, minSizes[index].width()) : 0;
}

int TilingAlgorithm::minHeightAt(const QVector<QSize> &minSizes, int index)
{
    return (index >= 0 && index < minSizes.size()) ? std::max(0, minSizes[index].height()) : 0;
}

TilingAlgorithm::ThreeColumnWidths TilingAlgorithm::solveThreeColumnWidths(
    int areaX, int contentWidth, int innerGap, qreal splitRatio,
    int minLeftWidth, int minCenterWidth, int minRightWidth)
{
    // Clamp center ratio so side columns are at least MinZoneSizePx wide
    const int minSideFloor = std::max(static_cast<int>(MinZoneSizePx),
                                      std::max(minLeftWidth, minRightWidth));
    const qreal maxCenter = std::min(
        static_cast<double>(MaxSplitRatio),
        1.0 - (2.0 * minSideFloor / static_cast<double>(contentWidth)));
    qreal centerRatio = std::clamp(splitRatio, MinSplitRatio, std::max(MinSplitRatio, maxCenter));

    // Ensure center satisfies its minimum
    if (minCenterWidth > 0) {
        const qreal minCenterRatio = static_cast<qreal>(minCenterWidth) / contentWidth;
        centerRatio = std::max(centerRatio, std::min(minCenterRatio, maxCenter));
    }

    const qreal sideRatio = (1.0 - centerRatio) / 2.0;

    int leftWidth = static_cast<int>(contentWidth * sideRatio);
    int centerWidth = static_cast<int>(contentWidth * centerRatio);
    int rightWidth = contentWidth - leftWidth - centerWidth;

    // Joint min-width solve for all three columns
    const int totalColumnMin = std::max(minLeftWidth, 0) + std::max(minCenterWidth, 0) + std::max(minRightWidth, 0);
    if (totalColumnMin > contentWidth && totalColumnMin > 0) {
        // Unsatisfiable: distribute proportionally by minimum weight
        const int effLeft = std::max(minLeftWidth, 1);
        const int effCenter = std::max(minCenterWidth, 1);
        const int effRight = std::max(minRightWidth, 1);
        const int effTotal = effLeft + effCenter + effRight;
        leftWidth = static_cast<int>(static_cast<qint64>(contentWidth) * effLeft / effTotal);
        centerWidth = static_cast<int>(static_cast<qint64>(contentWidth) * effCenter / effTotal);
        rightWidth = contentWidth - leftWidth - centerWidth;
    } else {
        if (minLeftWidth > 0 && leftWidth < minLeftWidth) {
            int deficit = minLeftWidth - leftWidth;
            leftWidth = minLeftWidth;
            int fromCenter = std::min(deficit, centerWidth - std::max(minCenterWidth, 1));
            fromCenter = std::max(0, fromCenter);
            centerWidth -= fromCenter;
            rightWidth = contentWidth - leftWidth - centerWidth;
        }
        if (minRightWidth > 0 && rightWidth < minRightWidth) {
            int deficit = minRightWidth - rightWidth;
            rightWidth = minRightWidth;
            int fromCenter = std::min(deficit, centerWidth - std::max(minCenterWidth, 1));
            fromCenter = std::max(0, fromCenter);
            centerWidth -= fromCenter;
            leftWidth = contentWidth - rightWidth - centerWidth;
        }
    }

    return ThreeColumnWidths{
        leftWidth, centerWidth, rightWidth,
        areaX,
        areaX + leftWidth + innerGap,
        areaX + leftWidth + innerGap + centerWidth + innerGap
    };
}

} // namespace PlasmaZones