// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "TilingAlgorithm.h"
#include "TilingState.h"
#include "config/configdefaults.h"
#include "core/constants.h"
#include "core/utils.h"
#include <algorithm>

namespace PlasmaZones {

using namespace AutotileDefaults;

QVector<WindowInfo> buildWindowInfos(const TilingState* state, int windowCount, int& focusedIndex)
{
    focusedIndex = -1;
    if (!state) {
        return {};
    }
    QVector<WindowInfo> infos;
    const QStringList windows = state->tiledWindows();
    const QString focusedWin = state->focusedWindow();
    infos.reserve(windowCount);
    for (int i = 0; i < windowCount && i < windows.size(); ++i) {
        WindowInfo info;
        info.appId = Utils::extractAppId(windows[i]);
        info.focused = (windows[i] == focusedWin);
        if (info.focused) {
            focusedIndex = i;
        }
        infos.append(info);
    }
    return infos;
}

TilingAlgorithm::TilingAlgorithm(QObject* parent)
    : QObject(parent)
{
}

int TilingAlgorithm::masterZoneIndex() const
{
    return -1; // Default: no master concept (subclasses override if they have one)
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
    return ConfigDefaults::autotileSplitRatio();
}

int TilingAlgorithm::minimumWindows() const
{
    return 1;
}

int TilingAlgorithm::defaultMaxWindows() const
{
    return ConfigDefaults::autotileMaxWindows();
}

bool TilingAlgorithm::producesOverlappingZones() const
{
    return false;
}

QString TilingAlgorithm::zoneNumberDisplay() const noexcept
{
    return QStringLiteral("all");
}

bool TilingAlgorithm::centerLayout() const
{
    return false;
}

bool TilingAlgorithm::isScripted() const noexcept
{
    return false;
}

bool TilingAlgorithm::isUserScript() const noexcept
{
    return false;
}

bool TilingAlgorithm::supportsMinSizes() const noexcept
{
    return true;
}

bool TilingAlgorithm::supportsMemory() const noexcept
{
    return false;
}

void TilingAlgorithm::prepareTilingState(TilingState* /*state*/) const
{
    // Default no-op. Memory-based algorithms override to ensure their SplitTree exists.
}

bool TilingAlgorithm::supportsLifecycleHooks() const noexcept
{
    return false;
}

void TilingAlgorithm::onWindowAdded(TilingState* /*state*/, int /*windowIndex*/)
{
    // Default no-op. Algorithms with lifecycle hooks override.
}

void TilingAlgorithm::onWindowRemoved(TilingState* /*state*/, int /*windowIndex*/)
{
    // Default no-op. Algorithms with lifecycle hooks override.
}

bool TilingAlgorithm::supportsCustomParams() const noexcept
{
    return false;
}

QVariantList TilingAlgorithm::customParamDefList() const
{
    return {};
}

bool TilingAlgorithm::hasCustomParam(const QString& /*name*/) const
{
    return false;
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

QRect TilingAlgorithm::innerRect(const QRect& screenGeometry, int outerGap)
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

QRect TilingAlgorithm::innerRect(const QRect& screenGeometry, const EdgeGaps& gaps)
{
    const int l = std::max(0, gaps.left);
    const int r = std::max(0, gaps.right);
    const int t = std::max(0, gaps.top);
    const int b = std::max(0, gaps.bottom);
    const int w = std::max(1, screenGeometry.width() - l - r);
    const int h = std::max(1, screenGeometry.height() - t - b);
    // When gaps exceed screen dimension, center the result to avoid placing
    // the rect off-screen (same behavior as the uniform overload)
    const int x = (l + r >= screenGeometry.width()) ? screenGeometry.left() + (screenGeometry.width() - w) / 2
                                                    : screenGeometry.left() + l;
    const int y = (t + b >= screenGeometry.height()) ? screenGeometry.top() + (screenGeometry.height() - h) / 2
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

QVector<int> TilingAlgorithm::distributeWithMinSizes(int total, int count, int gap, const QVector<int>& minDims)
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
                allocated = static_cast<int>(static_cast<qint64>(remaining) * mins[i] / remainingMin);
            } else {
                allocated = remaining / std::max(1, count - i);
            }
            allocated = std::max(1, std::min(allocated, remaining));
            sizes[i] = allocated;
            remaining -= allocated;
            remainingMin -= mins[i];
        }
    } else {
        // Satisfiable: try equal distribution first — if it already satisfies all
        // minimums, use it. This prevents constrained windows from dominating the
        // layout when the equal split already meets their requirements.
        QVector<int> equalSizes = distributeEvenly(available, count);
        bool equalSatisfies = true;
        for (int i = 0; i < count; ++i) {
            if (equalSizes[i] < mins[i]) {
                equalSatisfies = false;
                break;
            }
        }

        if (equalSatisfies) {
            sizes = equalSizes;
        } else {
            // Equal distribution violates at least one minimum. Give each item its
            // minimum, then distribute surplus only to unconstrained items (those
            // whose minimum is <= the equal share). This keeps constrained windows
            // at their minimum and gives maximum space to unconstrained ones.
            const int equalShare = available / count;
            int surplus = available - totalMin;

            // Identify which items are "unconstrained" (min <= equal share)
            int unconstrainedCount = 0;
            for (int i = 0; i < count; ++i) {
                sizes[i] = mins[i];
                if (mins[i] <= equalShare) {
                    ++unconstrainedCount;
                }
            }

            if (unconstrainedCount > 0 && surplus > 0) {
                // Distribute surplus only to unconstrained items
                const int base = surplus / unconstrainedCount;
                int remainder = surplus % unconstrainedCount;
                for (int i = 0; i < count; ++i) {
                    if (mins[i] <= equalShare) {
                        sizes[i] += base;
                        if (remainder > 0) {
                            ++sizes[i];
                            --remainder;
                        }
                    }
                }
            } else if (surplus > 0) {
                // All items are constrained (all mins > equal share) — distribute
                // surplus evenly since there are no "unconstrained" items
                const int base = surplus / count;
                int remainder = surplus % count;
                for (int i = 0; i < count; ++i) {
                    sizes[i] += base;
                    if (remainder > 0) {
                        ++sizes[i];
                        --remainder;
                    }
                }
            }
        }
    }

    return sizes;
}

int TilingAlgorithm::minWidthAt(const QVector<QSize>& minSizes, int index)
{
    return (index >= 0 && index < minSizes.size()) ? std::max(0, minSizes[index].width()) : 0;
}

int TilingAlgorithm::minHeightAt(const QVector<QSize>& minSizes, int index)
{
    return (index >= 0 && index < minSizes.size()) ? std::max(0, minSizes[index].height()) : 0;
}

void TilingAlgorithm::applyPerWindowMinSize(int& width, int& height, const QVector<QSize>& minSizes, int index)
{
    if (index < minSizes.size()) {
        if (minSizes[index].width() > 0) {
            width = std::max(width, minSizes[index].width());
        }
        if (minSizes[index].height() > 0) {
            height = std::max(height, minSizes[index].height());
        }
    }
}

void TilingAlgorithm::solveTwoPartMinSizes(int contentDim, int& firstDim, int& secondDim, int minFirst, int minSecond)
{
    const int totalMin = std::max(minFirst, 0) + std::max(minSecond, 0);
    if (totalMin > contentDim && totalMin > 0) {
        firstDim = static_cast<int>(static_cast<qint64>(contentDim) * std::max(minFirst, 1) / totalMin);
        secondDim = contentDim - firstDim;
    } else {
        if (minFirst > 0 && firstDim < minFirst) {
            firstDim = minFirst;
            secondDim = contentDim - firstDim;
        }
        if (minSecond > 0 && secondDim < minSecond) {
            secondDim = minSecond;
            firstDim = contentDim - secondDim;
        }
    }

    // Clamp to non-negative to prevent negative dimensions when mins exceed contentDim
    firstDim = std::max(0, firstDim);
    secondDim = std::max(0, secondDim);
}

TilingAlgorithm::ThreeColumnWidths TilingAlgorithm::solveThreeColumnWidths(int areaX, int contentWidth, int innerGap,
                                                                           qreal splitRatio, int minLeftWidth,
                                                                           int minCenterWidth, int minRightWidth)
{
    // Guard: degenerate content width (screen narrower than 2 * gap)
    if (contentWidth <= 0) {
        return ThreeColumnWidths{1, 1, 1, areaX, areaX, areaX};
    }

    // Clamp center ratio so side columns satisfy their individual minimums.
    // Use per-side minimums (left + right) instead of 2 * max(left, right)
    // to avoid over-constraining the center when only one side is constrained.
    const int effMinLeft = std::max(static_cast<int>(MinZoneSizePx), minLeftWidth);
    const int effMinRight = std::max(static_cast<int>(MinZoneSizePx), minRightWidth);
    const qreal maxCenter =
        std::min(static_cast<double>(MaxSplitRatio),
                 1.0 - (static_cast<double>(effMinLeft + effMinRight) / static_cast<double>(contentWidth)));
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
        // Enforce center minimum after side adjustments
        if (minCenterWidth > 0 && centerWidth < minCenterWidth) {
            int deficit = minCenterWidth - centerWidth;
            centerWidth = minCenterWidth;
            if (leftWidth >= rightWidth) {
                int take = std::min(deficit, leftWidth - 1);
                leftWidth -= take;
                deficit -= take;
                if (deficit > 0)
                    rightWidth -= deficit;
            } else {
                int take = std::min(deficit, rightWidth - 1);
                rightWidth -= take;
                deficit -= take;
                if (deficit > 0)
                    leftWidth -= deficit;
            }
        }
    }

    // Clamp all columns to at least 1px
    leftWidth = std::max(1, leftWidth);
    centerWidth = std::max(1, centerWidth);
    rightWidth = std::max(1, rightWidth);

    // Redistribute if 1px floor clamping caused sum to exceed contentWidth.
    // This can happen when tight constraints drive a column to 0 or negative,
    // then the 1px floor inflates the total by 1-2px.
    const int colSum = leftWidth + centerWidth + rightWidth;
    if (colSum > contentWidth) {
        const int excess = colSum - contentWidth;
        // Shrink the largest column to absorb the excess
        if (centerWidth >= leftWidth && centerWidth >= rightWidth) {
            centerWidth = std::max(1, centerWidth - excess);
        } else if (leftWidth >= rightWidth) {
            leftWidth = std::max(1, leftWidth - excess);
        } else {
            rightWidth = std::max(1, rightWidth - excess);
        }
    }

    return ThreeColumnWidths{leftWidth,
                             centerWidth,
                             rightWidth,
                             areaX,
                             areaX + leftWidth + innerGap,
                             areaX + leftWidth + innerGap + centerWidth + innerGap};
}

TilingAlgorithm::CumulativeMinDims
TilingAlgorithm::computeAlternatingCumulativeMinDims(int windowCount, const QVector<QSize>& minSizes, int innerGap)
{
    CumulativeMinDims result;
    result.minW.resize(windowCount + 1, 0);
    result.minH.resize(windowCount + 1, 0);

    if (minSizes.isEmpty()) {
        return result;
    }

    for (int i = windowCount - 1; i >= 0; --i) {
        const int mw = (i < minSizes.size()) ? std::max(0, minSizes[i].width()) : 0;
        const int mh = (i < minSizes.size()) ? std::max(0, minSizes[i].height()) : 0;
        const bool splitV = (i % 2 == 0);
        if (splitV) {
            result.minW[i] = mw + ((i < windowCount - 1 && result.minW[i + 1] > 0) ? innerGap + result.minW[i + 1] : 0);
            result.minH[i] = std::max(mh, result.minH[i + 1]);
        } else {
            result.minH[i] = mh + ((i < windowCount - 1 && result.minH[i + 1] > 0) ? innerGap + result.minH[i + 1] : 0);
            result.minW[i] = std::max(mw, result.minW[i + 1]);
        }
    }

    return result;
}

void TilingAlgorithm::appendGracefulDegradation(QVector<QRect>& zones, const QRect& remaining, int leftover,
                                                int innerGap)
{
    if (leftover <= 0) {
        return;
    }
    if (remaining.width() >= remaining.height()) {
        const int maxFit = std::max(1, remaining.width() / MinZoneSizePx);
        const int fitCount = std::min(leftover + 1, maxFit);
        QVector<int> widths = distributeWithGaps(remaining.width(), fitCount, innerGap);
        zones.last() = QRect(remaining.x(), remaining.y(), widths[0], remaining.height());
        int x = remaining.x() + widths[0] + innerGap;
        for (int j = 1; j < fitCount; ++j) {
            zones.append(QRect(x, remaining.y(), widths[j], remaining.height()));
            x += widths[j] + innerGap;
        }
        for (int j = fitCount; j <= leftover; ++j) {
            zones.append(zones.last());
        }
    } else {
        const int maxFit = std::max(1, remaining.height() / MinZoneSizePx);
        const int fitCount = std::min(leftover + 1, maxFit);
        QVector<int> heights = distributeWithGaps(remaining.height(), fitCount, innerGap);
        zones.last() = QRect(remaining.x(), remaining.y(), remaining.width(), heights[0]);
        int y = remaining.y() + heights[0] + innerGap;
        for (int j = 1; j < fitCount; ++j) {
            zones.append(QRect(remaining.x(), y, remaining.width(), heights[j]));
            y += heights[j] + innerGap;
        }
        for (int j = fitCount; j <= leftover; ++j) {
            zones.append(zones.last());
        }
    }
}

qreal TilingAlgorithm::clampOrProportionalFallback(qreal ratio, qreal minFirstRatio, qreal maxFirstRatio, int firstDim,
                                                   int secondDim)
{
    if (minFirstRatio <= maxFirstRatio) {
        return std::clamp(ratio, minFirstRatio, maxFirstRatio);
    }
    const int totalMin = firstDim + secondDim;
    if (totalMin > 0) {
        ratio = static_cast<qreal>(firstDim) / totalMin;
        return std::clamp(ratio, MinSplitRatio, MaxSplitRatio);
    }
    return ratio;
}

} // namespace PlasmaZones