// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ThreeColumnAlgorithm.h"
#include "../AlgorithmRegistry.h"
#include "../TilingState.h"
#include "core/constants.h"
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

QString ThreeColumnAlgorithm::name() const noexcept
{
    return QStringLiteral("Three Column");
}

QString ThreeColumnAlgorithm::description() const
{
    return tr("Center master with side columns");
}

QString ThreeColumnAlgorithm::icon() const noexcept
{
    return QStringLiteral("view-column-three");
}

QVector<QRect> ThreeColumnAlgorithm::calculateZones(int windowCount, const QRect &screenGeometry,
                                                     const TilingState &state) const
{
    QVector<QRect> zones;

    if (windowCount <= 0 || !screenGeometry.isValid()) {
        return zones;
    }

    const int screenX = screenGeometry.x();
    const int screenY = screenGeometry.y();
    const int screenWidth = screenGeometry.width();
    const int screenHeight = screenGeometry.height();

    // Single window takes full screen
    if (windowCount == 1) {
        zones.append(screenGeometry);
        return zones;
    }

    // Two windows: split in half (no center concept yet)
    if (windowCount == 2) {
        const int halfWidth = screenWidth / 2;
        zones.append(QRect(screenX, screenY, halfWidth, screenHeight));
        zones.append(QRect(screenX + halfWidth, screenY, screenWidth - halfWidth, screenHeight));
        return zones;
    }

    // Three or more windows: true three-column layout
    // Get split ratio from state (this controls center column width)
    const qreal centerRatio = std::clamp(state.splitRatio(), MinSplitRatio, MaxSplitRatio);
    const qreal sideRatio = (1.0 - centerRatio) / 2.0;

    const int leftWidth = static_cast<int>(screenWidth * sideRatio);
    const int centerWidth = static_cast<int>(screenWidth * centerRatio);
    const int rightWidth = screenWidth - leftWidth - centerWidth;

    const int leftX = screenX;
    const int centerX = screenX + leftWidth;
    const int rightX = screenX + leftWidth + centerWidth;

    // Distribute windows: first goes to center (master), rest alternate left/right
    // Window order: [center/master, left1, right1, left2, right2, ...]

    // Count windows for each column (excluding master)
    const int stackCount = windowCount - 1;
    const int leftCount = (stackCount + 1) / 2;  // Left gets extra if odd
    const int rightCount = stackCount - leftCount;

    // Calculate heights for left column
    QVector<int> leftHeights;
    if (leftCount > 0) {
        leftHeights = distributeEvenly(screenHeight, leftCount);
    }

    // Calculate heights for right column
    QVector<int> rightHeights;
    if (rightCount > 0) {
        rightHeights = distributeEvenly(screenHeight, rightCount);
    }

    // First zone: center/master (full height)
    zones.append(QRect(centerX, screenY, centerWidth, screenHeight));

    // Interleave left and right column windows
    int leftIdx = 0;
    int rightIdx = 0;
    int leftY = screenY;
    int rightY = screenY;

    for (int i = 0; i < stackCount; ++i) {
        if (i % 2 == 0 && leftIdx < leftCount) {
            // Add to left column
            zones.append(QRect(leftX, leftY, leftWidth, leftHeights[leftIdx]));
            leftY += leftHeights[leftIdx];
            ++leftIdx;
        } else if (rightIdx < rightCount) {
            // Add to right column
            zones.append(QRect(rightX, rightY, rightWidth, rightHeights[rightIdx]));
            rightY += rightHeights[rightIdx];
            ++rightIdx;
        } else if (leftIdx < leftCount) {
            // Fallback to left if right is full
            zones.append(QRect(leftX, leftY, leftWidth, leftHeights[leftIdx]));
            leftY += leftHeights[leftIdx];
            ++leftIdx;
        }
    }

    return zones;
}

} // namespace PlasmaZones
