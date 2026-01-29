// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "BSPAlgorithm.h"
#include "../TilingState.h"
#include "core/constants.h"
#include <cmath>

namespace PlasmaZones {

using namespace AutotileDefaults;

BSPAlgorithm::BSPAlgorithm(QObject *parent)
    : TilingAlgorithm(parent)
{
}

QString BSPAlgorithm::name() const
{
    return QStringLiteral("BSP");
}

QString BSPAlgorithm::description() const
{
    return tr("Binary space partitioning - recursive split layout");
}

QString BSPAlgorithm::icon() const
{
    return QStringLiteral("view-grid-symbolic");
}

QVector<QRect> BSPAlgorithm::calculateZones(int windowCount, const QRect &screenGeometry,
                                            const TilingState &state) const
{
    QVector<QRect> zones;

    if (windowCount <= 0 || !screenGeometry.isValid()) {
        return zones;
    }

    // Single window takes full screen
    if (windowCount == 1) {
        zones.append(screenGeometry);
        return zones;
    }

    // Get split ratio from state
    const qreal splitRatio = std::clamp(state.splitRatio(), MinSplitRatio, MaxSplitRatio);

    // Create root node with full screen
    auto root = std::make_unique<BSPNode>();
    root->geometry = screenGeometry;

    // Partition space for all windows
    partition(root.get(), windowCount, splitRatio);

    // Collect leaf geometries
    collectLeaves(root.get(), zones);

    return zones;
}

void BSPAlgorithm::partition(BSPNode *node, int windowsRemaining, qreal splitRatio) const
{
    if (!node || windowsRemaining <= 1) {
        return; // This node is a leaf (holds one window)
    }

    const QRect &geo = node->geometry;

    // Determine split direction based on aspect ratio
    // Split perpendicular to longest side for balanced regions
    const bool splitHorizontal = geo.height() > geo.width();

    // Calculate how many windows go to each child
    // First child gets ceiling of half, second gets the rest
    const int firstCount = (windowsRemaining + 1) / 2;
    const int secondCount = windowsRemaining - firstCount;

    // Create child nodes
    node->first = std::make_unique<BSPNode>();
    node->second = std::make_unique<BSPNode>();

    if (splitHorizontal) {
        // Split top/bottom
        const int splitY = geo.y() + static_cast<int>(geo.height() * splitRatio);

        node->first->geometry = QRect(geo.x(), geo.y(), geo.width(), splitY - geo.y());
        node->second->geometry = QRect(geo.x(), splitY, geo.width(), geo.bottom() - splitY + 1);
    } else {
        // Split left/right
        const int splitX = geo.x() + static_cast<int>(geo.width() * splitRatio);

        node->first->geometry = QRect(geo.x(), geo.y(), splitX - geo.x(), geo.height());
        node->second->geometry = QRect(splitX, geo.y(), geo.right() - splitX + 1, geo.height());
    }

    // Recursively partition children
    // Use 0.5 ratio for subsequent splits (only first split uses user ratio)
    partition(node->first.get(), firstCount, 0.5);
    partition(node->second.get(), secondCount, 0.5);
}

void BSPAlgorithm::collectLeaves(const BSPNode *node, QVector<QRect> &zones) const
{
    if (!node) {
        return;
    }

    if (node->isLeaf()) {
        zones.append(node->geometry);
    } else {
        collectLeaves(node->first.get(), zones);
        collectLeaves(node->second.get(), zones);
    }
}

} // namespace PlasmaZones
