// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "CascadeAlgorithm.h"
#include "../AlgorithmRegistry.h"
#include "../TilingState.h"
#include "core/constants.h"
#include "pz_i18n.h"
#include <cmath>

namespace PlasmaZones {

namespace {
AlgorithmRegistrar<CascadeAlgorithm> s_cascadeRegistrar(DBus::AutotileAlgorithm::Cascade, 120);
}

CascadeAlgorithm::CascadeAlgorithm(QObject* parent)
    : TilingAlgorithm(parent)
{
}

QString CascadeAlgorithm::name() const
{
    return PzI18n::tr("Cascade");
}

QString CascadeAlgorithm::description() const
{
    return PzI18n::tr("Overlapping windows in a diagonal cascade");
}

QString CascadeAlgorithm::icon() const noexcept
{
    return QStringLiteral("view-file-columns");
}

QVector<QRect> CascadeAlgorithm::calculateZones(const TilingParams& params) const
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

    // splitRatio controls the cascade offset as a fraction of area dimensions
    const qreal offsetRatio = params.state ? qBound(0.02, params.state->splitRatio(), 0.4) : 0.15;
    const int offsetX = std::max(20, qRound(area.width() * offsetRatio / (windowCount - 1)));
    const int offsetY = std::max(20, qRound(area.height() * offsetRatio / (windowCount - 1)));

    // Each window is sized to fill the area minus the total cascade offset
    const int totalOffsetX = offsetX * (windowCount - 1);
    const int totalOffsetY = offsetY * (windowCount - 1);
    const int winWidth = std::max(100, area.width() - totalOffsetX);
    const int winHeight = std::max(100, area.height() - totalOffsetY);

    for (int i = 0; i < windowCount; ++i) {
        const int x = area.x() + offsetX * i;
        const int y = area.y() + offsetY * i;
        zones.append(QRect(x, y, winWidth, winHeight));
    }

    return zones;
}

} // namespace PlasmaZones
