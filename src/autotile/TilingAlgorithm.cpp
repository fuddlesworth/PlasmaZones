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

} // namespace PlasmaZones