// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "MasterStackAlgorithm.h"
#include "../TilingState.h"
#include "core/constants.h"
#include <algorithm>

namespace PlasmaZones {

using namespace AutotileDefaults;

MasterStackAlgorithm::MasterStackAlgorithm(QObject *parent)
    : TilingAlgorithm(parent)
{
}

QString MasterStackAlgorithm::name() const
{
    return QStringLiteral("Master + Stack");
}

QString MasterStackAlgorithm::description() const
{
    return tr("Large master area with stacked secondary windows");
}

QString MasterStackAlgorithm::icon() const
{
    return QStringLiteral("view-split-left-right");
}

int MasterStackAlgorithm::masterZoneIndex() const
{
    return 0; // First zone is always master
}

bool MasterStackAlgorithm::supportsMasterCount() const
{
    return true;
}

bool MasterStackAlgorithm::supportsSplitRatio() const
{
    return true;
}

qreal MasterStackAlgorithm::defaultSplitRatio() const
{
    return DefaultSplitRatio; // 0.6 (60% master)
}

QVector<QRect> MasterStackAlgorithm::calculateZones(int windowCount, const QRect &screenGeometry,
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

    // Get master count and split ratio from state
    const int masterCount = std::clamp(state.masterCount(), 1, windowCount);
    const int stackCount = windowCount - masterCount;
    const qreal splitRatio = std::clamp(state.splitRatio(), MinSplitRatio, MaxSplitRatio);

    // Calculate master and stack widths
    int masterWidth;
    int stackWidth;

    if (stackCount == 0) {
        // All windows are masters - they take full width
        masterWidth = screenWidth;
        stackWidth = 0;
    } else {
        masterWidth = static_cast<int>(screenWidth * splitRatio);
        stackWidth = screenWidth - masterWidth;
    }

    // Calculate master zone heights (divide evenly)
    const int masterHeight = screenHeight / masterCount;
    int masterRemainder = screenHeight % masterCount;

    // Generate master zones (left side, stacked vertically)
    int currentY = screenY;
    for (int i = 0; i < masterCount; ++i) {
        int zoneHeight = masterHeight;
        // Distribute remainder pixels to first zones
        if (masterRemainder > 0) {
            ++zoneHeight;
            --masterRemainder;
        }

        zones.append(QRect(screenX, currentY, masterWidth, zoneHeight));
        currentY += zoneHeight;
    }

    // Generate stack zones (right side, stacked vertically)
    if (stackCount > 0) {
        const int stackHeight = screenHeight / stackCount;
        int stackRemainder = screenHeight % stackCount;
        const int stackX = screenX + masterWidth;

        currentY = screenY;
        for (int i = 0; i < stackCount; ++i) {
            int zoneHeight = stackHeight;
            // Distribute remainder pixels to first zones
            if (stackRemainder > 0) {
                ++zoneHeight;
                --stackRemainder;
            }

            zones.append(QRect(stackX, currentY, stackWidth, zoneHeight));
            currentY += zoneHeight;
        }
    }

    return zones;
}

} // namespace PlasmaZones
