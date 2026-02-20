// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ColumnsAlgorithm.h"
#include "../AlgorithmRegistry.h"
#include "../TilingState.h"
#include "core/constants.h"
#include <KLocalizedString>

namespace PlasmaZones {

// Self-registration: Columns is the simplest layout (priority 20)
namespace {
AlgorithmRegistrar<ColumnsAlgorithm> s_columnsRegistrar(DBus::AutotileAlgorithm::Columns, 20);
}

ColumnsAlgorithm::ColumnsAlgorithm(QObject *parent)
    : TilingAlgorithm(parent)
{
}

QString ColumnsAlgorithm::name() const
{
    return i18n("Columns");
}

QString ColumnsAlgorithm::description() const
{
    return i18n("Equal-width vertical columns");
}

QString ColumnsAlgorithm::icon() const noexcept
{
    return QStringLiteral("view-split-left-right");
}

QVector<QRect> ColumnsAlgorithm::calculateZones(const TilingParams &params) const
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

    // Extract per-window minimum widths
    QVector<int> minWidths;
    if (!minSizes.isEmpty()) {
        minWidths.resize(windowCount);
        for (int i = 0; i < windowCount; ++i) {
            minWidths[i] = (i < minSizes.size()) ? minSizes[i].width() : 0;
        }
    }

    // Calculate column widths with gaps and minimum sizes
    const QVector<int> columnWidths = minWidths.isEmpty()
        ? distributeWithGaps(area.width(), windowCount, innerGap)
        : distributeWithMinSizes(area.width(), windowCount, innerGap, minWidths);

    int currentX = area.x();
    for (int i = 0; i < windowCount; ++i) {
        zones.append(QRect(currentX, area.y(), columnWidths[i], area.height()));
        currentX += columnWidths[i] + innerGap;
    }

    return zones;
}

} // namespace PlasmaZones
