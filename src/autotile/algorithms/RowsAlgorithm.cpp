// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "RowsAlgorithm.h"
#include "../AlgorithmRegistry.h"
#include "../TilingState.h"
#include "core/constants.h"
#include <KLocalizedString>

namespace PlasmaZones {

// Self-registration: Rows provides simple horizontal stacking (priority 25)
namespace {
AlgorithmRegistrar<RowsAlgorithm> s_rowsRegistrar(DBus::AutotileAlgorithm::Rows, 25);
}

RowsAlgorithm::RowsAlgorithm(QObject *parent)
    : TilingAlgorithm(parent)
{
}

QString RowsAlgorithm::name() const
{
    return i18n("Rows");
}

QString RowsAlgorithm::description() const
{
    return i18n("Equal-height horizontal rows");
}

QString RowsAlgorithm::icon() const noexcept
{
    return QStringLiteral("view-split-top-bottom");
}

QVector<QRect> RowsAlgorithm::calculateZones(const TilingParams &params) const
{
    const int windowCount = params.windowCount;
    const auto &screenGeometry = params.screenGeometry;
    const int innerGap = params.innerGap;
    const int outerGap = params.outerGap;
    const auto &minSizes = params.minSizes;

    QVector<QRect> zones;

    if (windowCount <= 0 || !screenGeometry.isValid()) {
        return zones;
    }

    const QRect area = innerRect(screenGeometry, outerGap);

    // Single window takes full available area
    if (windowCount == 1) {
        zones.append(area);
        return zones;
    }

    // Extract per-window minimum heights
    QVector<int> minHeights;
    if (!minSizes.isEmpty()) {
        minHeights.resize(windowCount);
        for (int i = 0; i < windowCount; ++i) {
            minHeights[i] = (i < minSizes.size()) ? minSizes[i].height() : 0;
        }
    }

    // Calculate row heights with gaps and minimum sizes
    const QVector<int> rowHeights = minHeights.isEmpty()
        ? distributeWithGaps(area.height(), windowCount, innerGap)
        : distributeWithMinSizes(area.height(), windowCount, innerGap, minHeights);

    int currentY = area.y();
    for (int i = 0; i < windowCount; ++i) {
        zones.append(QRect(area.x(), currentY, area.width(), rowHeights[i]));
        currentY += rowHeights[i] + innerGap;
    }

    return zones;
}

} // namespace PlasmaZones
