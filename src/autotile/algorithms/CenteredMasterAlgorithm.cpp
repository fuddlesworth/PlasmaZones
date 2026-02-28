// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "CenteredMasterAlgorithm.h"
#include "../AlgorithmRegistry.h"
#include "../TilingState.h"
#include "core/constants.h"
#include <KLocalizedString>
#include <algorithm>
#include <cmath>

namespace PlasmaZones {

using namespace AutotileDefaults;

// Self-registration: Centered Master (alphabetical priority 20)
namespace {
AlgorithmRegistrar<CenteredMasterAlgorithm> s_centeredMasterRegistrar(DBus::AutotileAlgorithm::CenteredMaster, 20);
}

CenteredMasterAlgorithm::CenteredMasterAlgorithm(QObject *parent)
    : TilingAlgorithm(parent)
{
}

QString CenteredMasterAlgorithm::name() const
{
    return i18n("Centered Master");
}

QString CenteredMasterAlgorithm::description() const
{
    return i18n("Master windows centered with stacks on both sides");
}

QString CenteredMasterAlgorithm::icon() const noexcept
{
    return QStringLiteral("view-split-left-right");
}

QVector<QRect> CenteredMasterAlgorithm::calculateZones(const TilingParams &params) const
{
    const int windowCount = params.windowCount;
    const auto &screenGeometry = params.screenGeometry;
    const int innerGap = params.innerGap;
    const auto &outerGaps = params.outerGaps;
    const auto &minSizes = params.minSizes;

    QVector<QRect> zones;

    if (windowCount <= 0 || !screenGeometry.isValid() || !params.state) {
        return zones;
    }

    const auto &state = *params.state;

    const QRect area = innerRect(screenGeometry, outerGaps);

    // Single window takes full available area
    if (windowCount == 1) {
        zones.append(area);
        return zones;
    }

    const int masterCount = std::clamp(state.masterCount(), 1, windowCount);
    const int stackCount = windowCount - masterCount;
    const qreal splitRatio = std::clamp(state.splitRatio(), MinSplitRatio, MaxSplitRatio);

    // Only masters — stack vertically, full width
    if (stackCount == 0) {
        const QVector<int> masterHeights = distributeWithGaps(area.height(), masterCount, innerGap);
        int currentY = area.y();
        for (int i = 0; i < masterCount; ++i) {
            zones.append(QRect(area.x(), currentY, area.width(), masterHeights[i]));
            currentY += masterHeights[i] + innerGap;
        }
        return zones;
    }

    // One stack window: 2-column layout (master left, stack right)
    if (stackCount == 1) {
        const int contentWidth = area.width() - innerGap;
        int masterWidth = static_cast<int>(contentWidth * splitRatio);
        int stackWidth = contentWidth - masterWidth;

        // Min-width clamping
        if (!minSizes.isEmpty()) {
            const int minMW = minWidthAt(minSizes, 0);
            const int minSW = minWidthAt(minSizes, masterCount);
            const int totalMin = std::max(minMW, 0) + std::max(minSW, 0);
            if (totalMin > contentWidth && totalMin > 0) {
                masterWidth = static_cast<int>(
                    static_cast<qint64>(contentWidth) * std::max(minMW, 1) / totalMin);
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

        // Masters stacked vertically on left
        const QVector<int> masterHeights = distributeWithGaps(area.height(), masterCount, innerGap);
        int currentY = area.y();
        for (int i = 0; i < masterCount; ++i) {
            zones.append(QRect(area.x(), currentY, masterWidth, masterHeights[i]));
            currentY += masterHeights[i] + innerGap;
        }

        // Single stack on right
        zones.append(QRect(area.x() + masterWidth + innerGap, area.y(), stackWidth, area.height()));
        return zones;
    }

    // 3-column layout: left stack, center masters, right stack
    const int leftCount = static_cast<int>(std::ceil(static_cast<double>(stackCount) / 2.0));
    const int rightCount = stackCount - leftCount;

    // Deduct two vertical gaps (left|center and center|right)
    const int contentWidth = area.width() - 2 * innerGap;

    if (contentWidth <= 0) {
        // Degenerate: screen too narrow
        for (int i = 0; i < windowCount; ++i)
            zones.append(area);
        return zones;
    }

    // Compute per-column minimum widths
    int minCenterWidth = 0;
    int minLeftWidth = 0;
    int minRightWidth = 0;
    if (!minSizes.isEmpty()) {
        // Masters are zones 0..masterCount-1
        for (int i = 0; i < masterCount && i < minSizes.size(); ++i) {
            minCenterWidth = std::max(minCenterWidth, minSizes[i].width());
        }
        // Stack interleaving: zone masterCount+0→left, +1→right, +2→left, ...
        int li = 0, ri = 0;
        for (int i = 0; i < stackCount; ++i) {
            int zoneIdx = masterCount + i;
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

    const auto cols = solveThreeColumnWidths(
        area.x(), contentWidth, innerGap, splitRatio,
        minLeftWidth, minCenterWidth, minRightWidth);

    const int leftWidth = cols.leftWidth;
    const int centerWidth = cols.centerWidth;
    const int rightWidth = cols.rightWidth;
    const int leftX = cols.leftX;
    const int centerX = cols.centerX;
    const int rightX = cols.rightX;

    // Calculate heights for each column
    const QVector<int> masterHeights = distributeWithGaps(area.height(), masterCount, innerGap);
    const QVector<int> leftHeights = distributeWithGaps(area.height(), leftCount, innerGap);
    QVector<int> rightHeights;
    if (rightCount > 0) {
        rightHeights = distributeWithGaps(area.height(), rightCount, innerGap);
    }

    // Masters in center column (stacked vertically)
    int currentY = area.y();
    for (int i = 0; i < masterCount; ++i) {
        zones.append(QRect(centerX, currentY, centerWidth, masterHeights[i]));
        currentY += masterHeights[i] + innerGap;
    }

    // Interleave left and right stack windows
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
