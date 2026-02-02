// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "RowsAlgorithm.h"
#include "../AlgorithmRegistry.h"
#include "../TilingState.h"
#include "core/constants.h"

namespace PlasmaZones {

// Self-registration: Rows provides simple horizontal stacking (priority 25)
namespace {
AlgorithmRegistrar<RowsAlgorithm> s_rowsRegistrar(DBus::AutotileAlgorithm::Rows, 25);
}

RowsAlgorithm::RowsAlgorithm(QObject *parent)
    : TilingAlgorithm(parent)
{
}

QString RowsAlgorithm::name() const noexcept
{
    return QStringLiteral("Rows");
}

QString RowsAlgorithm::description() const
{
    return tr("Equal-height horizontal rows");
}

QString RowsAlgorithm::icon() const noexcept
{
    return QStringLiteral("view-split-top-bottom");
}

QVector<QRect> RowsAlgorithm::calculateZones(int windowCount, const QRect &screenGeometry,
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

    // Calculate row heights using helper for pixel-perfect distribution
    const QVector<int> rowHeights = distributeEvenly(screenHeight, windowCount);

    int currentY = screenY;
    for (int i = 0; i < windowCount; ++i) {
        zones.append(QRect(screenX, currentY, screenWidth, rowHeights[i]));
        currentY += rowHeights[i];
    }

    return zones;
}

} // namespace PlasmaZones
