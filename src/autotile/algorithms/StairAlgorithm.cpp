// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "StairAlgorithm.h"
#include "../AlgorithmRegistry.h"
#include "../TilingState.h"
#include "core/constants.h"
#include "pz_i18n.h"
#include <cmath>

namespace PlasmaZones {

namespace {
AlgorithmRegistrar<StairAlgorithm> s_stairRegistrar(DBus::AutotileAlgorithm::Stair, 130);
}

StairAlgorithm::StairAlgorithm(QObject* parent)
    : TilingAlgorithm(parent)
{
}

QString StairAlgorithm::name() const
{
    return PzI18n::tr("Stair");
}

QString StairAlgorithm::description() const
{
    return PzI18n::tr("Stepped staircase arrangement");
}

QString StairAlgorithm::icon() const noexcept
{
    return QStringLiteral("go-down-skip");
}

QVector<QRect> StairAlgorithm::calculateZones(const TilingParams& params) const
{
    const int windowCount = params.windowCount;
    const auto& screenGeometry = params.screenGeometry;
    const auto& outerGaps = params.outerGaps;

    QVector<QRect> zones;

    if (windowCount <= 0 || !screenGeometry.isValid()) {
        return zones;
    }

    const QRect area = innerRect(screenGeometry, outerGaps);

    if (windowCount == 1) {
        zones.append(area);
        return zones;
    }

    // splitRatio controls window size relative to screen (0.3 = 30% of screen per window)
    const qreal sizeRatio = params.state ? qBound(0.3, params.state->splitRatio(), 0.8) : 0.5;

    // All windows are the same size
    const int winWidth = std::max(100, qRound(area.width() * sizeRatio));
    const int winHeight = std::max(100, qRound(area.height() * sizeRatio));

    // Diagonal offset distributes the remaining space evenly across steps
    const int totalOffsetX = area.width() - winWidth;
    const int totalOffsetY = area.height() - winHeight;
    const int stepX = (windowCount > 1) ? totalOffsetX / (windowCount - 1) : 0;
    const int stepY = (windowCount > 1) ? totalOffsetY / (windowCount - 1) : 0;

    for (int i = 0; i < windowCount; ++i) {
        const int x = area.x() + stepX * i;
        const int y = area.y() + stepY * i;
        zones.append(QRect(x, y, winWidth, winHeight));
    }

    return zones;
}

} // namespace PlasmaZones
