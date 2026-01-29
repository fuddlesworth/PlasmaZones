// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ColumnsAlgorithm.h"
#include "../TilingState.h"

namespace PlasmaZones {

ColumnsAlgorithm::ColumnsAlgorithm(QObject *parent)
    : TilingAlgorithm(parent)
{
}

QString ColumnsAlgorithm::name() const noexcept
{
    return QStringLiteral("Columns");
}

QString ColumnsAlgorithm::description() const
{
    return tr("Equal-width vertical columns");
}

QString ColumnsAlgorithm::icon() const noexcept
{
    return QStringLiteral("view-split-left-right");
}

QVector<QRect> ColumnsAlgorithm::calculateZones(int windowCount, const QRect &screenGeometry,
                                                const TilingState & /*state*/) const
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

    // Calculate column widths using helper for pixel-perfect distribution
    const QVector<int> columnWidths = distributeEvenly(screenWidth, windowCount);

    int currentX = screenX;
    for (int i = 0; i < windowCount; ++i) {
        zones.append(QRect(currentX, screenY, columnWidths[i], screenHeight));
        currentX += columnWidths[i];
    }

    return zones;
}

} // namespace PlasmaZones
