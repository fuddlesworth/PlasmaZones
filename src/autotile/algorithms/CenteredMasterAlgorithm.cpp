// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "CenteredMasterAlgorithm.h"
#include "../AlgorithmRegistry.h"
#include "../TilingState.h"
#include "core/constants.h"
#include "pz_i18n.h"
#include <algorithm>
#include <cmath>

namespace PlasmaZones {

using namespace AutotileDefaults;

// Self-registration: Centered Master (alphabetical priority 20)
namespace {
AlgorithmRegistrar<CenteredMasterAlgorithm> s_centeredMasterRegistrar(DBus::AutotileAlgorithm::CenteredMaster, 20);
}

CenteredMasterAlgorithm::CenteredMasterAlgorithm(QObject* parent)
    : TilingAlgorithm(parent)
{
}

QString CenteredMasterAlgorithm::name() const
{
    return PzI18n::tr("Centered Master");
}

QString CenteredMasterAlgorithm::description() const
{
    return PzI18n::tr("Master windows centered with stacks on both sides");
}

QVector<QRect> CenteredMasterAlgorithm::calculateZones(const TilingParams& params) const
{
    const int windowCount = params.windowCount;
    const auto& screenGeometry = params.screenGeometry;
    const int innerGap = params.innerGap;
    const auto& outerGaps = params.outerGaps;
    const auto& minSizes = params.minSizes;

    QVector<QRect> zones;

    if (windowCount <= 0 || !screenGeometry.isValid() || !params.state) {
        return zones;
    }

    const auto& state = *params.state;

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
            solveTwoPartMinSizes(contentWidth, masterWidth, stackWidth, minMW, minSW);
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

    // Precompute stack-to-side mapping (used for width, height, and zone assignment)
    QVector<bool> stackIsLeft(stackCount);
    {
        int li = 0, ri = 0;
        for (int i = 0; i < stackCount; ++i) {
            if (i % 2 == 0 && li < leftCount) {
                stackIsLeft[i] = true;
                ++li;
            } else if (ri < rightCount) {
                stackIsLeft[i] = false;
                ++ri;
            } else {
                stackIsLeft[i] = true;
                ++li;
            }
        }
    }

    // Compute per-column minimum widths
    int minCenterWidth = 0;
    int minLeftWidth = 0;
    int minRightWidth = 0;
    if (!minSizes.isEmpty()) {
        for (int i = 0; i < masterCount && i < minSizes.size(); ++i) {
            minCenterWidth = std::max(minCenterWidth, minSizes[i].width());
        }
        for (int i = 0; i < stackCount; ++i) {
            int zoneIdx = masterCount + i;
            if (zoneIdx < minSizes.size()) {
                if (stackIsLeft[i]) {
                    minLeftWidth = std::max(minLeftWidth, minSizes[zoneIdx].width());
                } else {
                    minRightWidth = std::max(minRightWidth, minSizes[zoneIdx].width());
                }
            }
        }
    }

    const auto cols = solveThreeColumnWidths(area.x(), contentWidth, innerGap, splitRatio, minLeftWidth, minCenterWidth,
                                             minRightWidth);

    const int leftWidth = cols.leftWidth;
    const int centerWidth = cols.centerWidth;
    const int rightWidth = cols.rightWidth;
    const int leftX = cols.leftX;
    const int centerX = cols.centerX;
    const int rightX = cols.rightX;

    // Calculate heights for each column, respecting per-window minHeight
    QVector<int> masterMinH, leftMinH, rightMinH;
    if (!minSizes.isEmpty()) {
        for (int i = 0; i < masterCount; ++i) {
            masterMinH.append(minHeightAt(minSizes, i));
        }
        for (int i = 0; i < stackCount; ++i) {
            int mh = minHeightAt(minSizes, masterCount + i);
            if (stackIsLeft[i]) {
                leftMinH.append(mh);
            } else {
                rightMinH.append(mh);
            }
        }
    }
    const QVector<int> masterHeights = masterMinH.isEmpty()
        ? distributeWithGaps(area.height(), masterCount, innerGap)
        : distributeWithMinSizes(area.height(), masterCount, innerGap, masterMinH);
    const QVector<int> leftHeights = leftMinH.isEmpty()
        ? distributeWithGaps(area.height(), leftCount, innerGap)
        : distributeWithMinSizes(area.height(), leftCount, innerGap, leftMinH);
    QVector<int> rightHeights;
    if (rightCount > 0) {
        rightHeights = rightMinH.isEmpty() ? distributeWithGaps(area.height(), rightCount, innerGap)
                                           : distributeWithMinSizes(area.height(), rightCount, innerGap, rightMinH);
    }

    // Masters in center column (stacked vertically)
    int currentY = area.y();
    for (int i = 0; i < masterCount; ++i) {
        zones.append(QRect(centerX, currentY, centerWidth, masterHeights[i]));
        currentY += masterHeights[i] + innerGap;
    }

    // Assign stack windows using precomputed side mapping
    int leftIdx = 0;
    int rightIdx = 0;
    int leftY = area.y();
    int rightY = area.y();

    for (int i = 0; i < stackCount; ++i) {
        if (stackIsLeft[i]) {
            zones.append(QRect(leftX, leftY, leftWidth, leftHeights[leftIdx]));
            leftY += leftHeights[leftIdx] + innerGap;
            ++leftIdx;
        } else {
            zones.append(QRect(rightX, rightY, rightWidth, rightHeights[rightIdx]));
            rightY += rightHeights[rightIdx] + innerGap;
            ++rightIdx;
        }
    }

    return zones;
}

} // namespace PlasmaZones
