// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ThreeColumnAlgorithm.h"
#include "../AlgorithmRegistry.h"
#include "../TilingState.h"
#include "core/constants.h"
#include <KLocalizedString>
#include <cmath>

namespace PlasmaZones {

using namespace AutotileDefaults;

// Self-registration: Three Column provides centered master layout (priority 45)
namespace {
AlgorithmRegistrar<ThreeColumnAlgorithm> s_threeColumnRegistrar(DBus::AutotileAlgorithm::ThreeColumn, 45);
}

ThreeColumnAlgorithm::ThreeColumnAlgorithm(QObject *parent)
    : TilingAlgorithm(parent)
{
}

QString ThreeColumnAlgorithm::name() const
{
    return i18n("Three Column");
}

QString ThreeColumnAlgorithm::description() const
{
    return i18n("Center master with side columns");
}

QString ThreeColumnAlgorithm::icon() const noexcept
{
    return QStringLiteral("view-column-three");
}

QVector<QRect> ThreeColumnAlgorithm::calculateZones(const TilingParams &params) const
{
    const int windowCount = params.windowCount;
    const auto &screenGeometry = params.screenGeometry;
    const int innerGap = params.innerGap;
    const int outerGap = params.outerGap;
    const auto &minSizes = params.minSizes;

    QVector<QRect> zones;

    if (windowCount <= 0 || !screenGeometry.isValid() || !params.state) {
        return zones;
    }

    const auto &state = *params.state;

    const QRect area = innerRect(screenGeometry, outerGap);

    // Single window takes full available area
    if (windowCount == 1) {
        zones.append(area);
        return zones;
    }

    // Fall back to equal columns if screen is too narrow for three columns
    if (windowCount >= 3 && area.width() < 3 * MinZoneSizePx) {
        const QVector<int> widths = distributeWithGaps(area.width(), windowCount, innerGap);
        int x = area.x();
        for (int i = 0; i < windowCount; ++i) {
            zones.append(QRect(x, area.y(), widths[i], area.height()));
            x += widths[i] + innerGap;
        }
        return zones;
    }

    // Two windows: simple left/right split with gap between
    if (windowCount == 2) {
        const qreal ratio = std::clamp(state.splitRatio(), MinSplitRatio, MaxSplitRatio);
        const int contentWidth = std::max(1, area.width() - innerGap);
        int masterWidth = static_cast<int>(contentWidth * ratio);
        int stackWidth = contentWidth - masterWidth;

        // Joint min-width solve for 2-window case
        if (!minSizes.isEmpty()) {
            const int minMW = minWidthAt(minSizes, 0);
            const int minSW = minWidthAt(minSizes, 1);
            const int totalMin2 = minMW + minSW;
            if (totalMin2 > contentWidth && totalMin2 > 0) {
                // Unsatisfiable: distribute proportionally
                masterWidth = static_cast<int>(
                    static_cast<qint64>(contentWidth) * std::max(minMW, 1) / totalMin2);
                stackWidth = contentWidth - masterWidth;
            } else {
                if (minMW > 0 && masterWidth < minMW) {
                    masterWidth = minMW;
                    stackWidth = contentWidth - masterWidth;
                }
                if (minSW > 0 && stackWidth < minSW) {
                    stackWidth = minSW;
                    masterWidth = contentWidth - stackWidth;
                }
            }
        }

        zones.append(QRect(area.x(), area.y(), masterWidth, area.height()));
        zones.append(QRect(area.x() + masterWidth + innerGap, area.y(), stackWidth, area.height()));
        return zones;
    }

    // Three or more windows: true three-column layout
    // Deduct two vertical gaps (left|center and center|right)
    const int contentWidth = area.width() - 2 * innerGap;

    // Count windows for each column (excluding master)
    const int stackCount = windowCount - 1;
    const int leftCount = (stackCount + 1) / 2;  // Left gets extra if odd
    const int rightCount = stackCount - leftCount;

    // Compute per-column minimum widths from minSizes
    // Zone ordering: [center(0), left1(1), right1(2), left2(3), right2(4), ...]
    int minCenterWidth = 0;
    int minLeftWidth = 0;
    int minRightWidth = 0;
    if (!minSizes.isEmpty()) {
        if (minSizes.size() > 0) {
            minCenterWidth = minSizes[0].width();
        }
        // Interleaved: zone 1,3,5,... are left; zone 2,4,6,... are right
        int li = 0, ri = 0;
        for (int i = 0; i < stackCount; ++i) {
            int zoneIdx = i + 1; // skip center at index 0
            if (i % 2 == 0 && li < leftCount) {
                if (zoneIdx < minSizes.size()) {
                    minLeftWidth = std::max(minLeftWidth, minSizes[zoneIdx].width());
                }
                ++li;
            } else if (ri < rightCount) {
                if (zoneIdx < minSizes.size()) {
                    minRightWidth = std::max(minRightWidth, minSizes[zoneIdx].width());
                }
                ++ri;
            } else if (li < leftCount) {
                if (zoneIdx < minSizes.size()) {
                    minLeftWidth = std::max(minLeftWidth, minSizes[zoneIdx].width());
                }
                ++li;
            }
        }
    }

    // Clamp center ratio so side columns are at least MinZoneSizePx wide
    const int minSideFloor = std::max(static_cast<int>(MinZoneSizePx),
                                      std::max(minLeftWidth, minRightWidth));
    const qreal maxCenter = std::min(
        static_cast<double>(MaxSplitRatio),
        1.0 - (2.0 * minSideFloor / static_cast<double>(contentWidth)));
    qreal centerRatio = std::clamp(state.splitRatio(), MinSplitRatio, std::max(MinSplitRatio, maxCenter));

    // Also ensure center satisfies its minimum
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

    const int leftX = area.x();
    const int centerX = area.x() + leftWidth + innerGap;
    const int rightX = area.x() + leftWidth + innerGap + centerWidth + innerGap;

    // Build per-column min heights from minSizes interleaving order
    QVector<int> leftMinHeights;
    QVector<int> rightMinHeights;
    if (!minSizes.isEmpty()) {
        leftMinHeights.resize(leftCount, 0);
        rightMinHeights.resize(rightCount, 0);
        int li = 0, ri = 0;
        for (int i = 0; i < stackCount; ++i) {
            int zoneIdx = i + 1;
            if (i % 2 == 0 && li < leftCount) {
                if (zoneIdx < minSizes.size()) {
                    leftMinHeights[li] = minSizes[zoneIdx].height();
                }
                ++li;
            } else if (ri < rightCount) {
                if (zoneIdx < minSizes.size()) {
                    rightMinHeights[ri] = minSizes[zoneIdx].height();
                }
                ++ri;
            } else if (li < leftCount) {
                if (zoneIdx < minSizes.size()) {
                    leftMinHeights[li] = minSizes[zoneIdx].height();
                }
                ++li;
            }
        }
    }

    // Calculate heights with gaps between vertically stacked zones
    QVector<int> leftHeights;
    if (leftCount > 0) {
        leftHeights = leftMinHeights.isEmpty()
            ? distributeWithGaps(area.height(), leftCount, innerGap)
            : distributeWithMinSizes(area.height(), leftCount, innerGap, leftMinHeights);
    }

    QVector<int> rightHeights;
    if (rightCount > 0) {
        rightHeights = rightMinHeights.isEmpty()
            ? distributeWithGaps(area.height(), rightCount, innerGap)
            : distributeWithMinSizes(area.height(), rightCount, innerGap, rightMinHeights);
    }

    // First zone: center/master (full height)
    zones.append(QRect(centerX, area.y(), centerWidth, area.height()));

    // Interleave left and right column windows
    int leftIdx = 0;
    int rightIdx = 0;
    int leftY = area.y();
    int rightY = area.y();

    for (int i = 0; i < stackCount; ++i) {
        if (i % 2 == 0 && leftIdx < leftCount) {
            zones.append(QRect(leftX, leftY, leftWidth, leftHeights[leftIdx]));
            leftY += leftHeights[leftIdx] + innerGap;
            ++leftIdx;
        } else if (rightIdx < rightCount) {
            zones.append(QRect(rightX, rightY, rightWidth, rightHeights[rightIdx]));
            rightY += rightHeights[rightIdx] + innerGap;
            ++rightIdx;
        } else if (leftIdx < leftCount) {
            zones.append(QRect(leftX, leftY, leftWidth, leftHeights[leftIdx]));
            leftY += leftHeights[leftIdx] + innerGap;
            ++leftIdx;
        }
    }

    return zones;
}

} // namespace PlasmaZones
