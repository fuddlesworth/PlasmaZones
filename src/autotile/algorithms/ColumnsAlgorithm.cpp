// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ColumnsAlgorithm.h"
#include "../TilingState.h"

namespace PlasmaZones {

ColumnsAlgorithm::ColumnsAlgorithm(QObject *parent)
    : TilingAlgorithm(parent)
{
}

QString ColumnsAlgorithm::name() const
{
    return QStringLiteral("Columns");
}

QString ColumnsAlgorithm::description() const
{
    return tr("Equal-width vertical columns");
}

QString ColumnsAlgorithm::icon() const
{
    return QStringLiteral("view-split-left-right");
}

QVector<QRect> ColumnsAlgorithm::calculateZones(int windowCount, const QRect &screenGeometry,
                                                const TilingState &state) const
{
    Q_UNUSED(state) // Columns doesn't use master count or split ratio

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

    // Calculate column width with remainder distribution
    const int columnWidth = screenWidth / windowCount;
    int remainder = screenWidth % windowCount;

    int currentX = screenX;
    for (int i = 0; i < windowCount; ++i) {
        int width = columnWidth;
        // Distribute remainder pixels to first columns
        if (remainder > 0) {
            ++width;
            --remainder;
        }

        zones.append(QRect(currentX, screenY, width, screenHeight));
        currentX += width;
    }

    return zones;
}

} // namespace PlasmaZones
