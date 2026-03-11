// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "SpreadAlgorithm.h"
#include "../AlgorithmRegistry.h"
#include "../TilingState.h"
#include "core/constants.h"
#include <KLocalizedString>
#include <cmath>

namespace PlasmaZones {

namespace {
AlgorithmRegistrar<SpreadAlgorithm> s_spreadRegistrar(DBus::AutotileAlgorithm::Spread, 140);
}

SpreadAlgorithm::SpreadAlgorithm(QObject* parent)
    : TilingAlgorithm(parent)
{
}

QString SpreadAlgorithm::name() const
{
    return i18n("Spread");
}

QString SpreadAlgorithm::description() const
{
    return i18n("Windows spread evenly across the screen");
}

QString SpreadAlgorithm::icon() const noexcept
{
    return QStringLiteral("distribute-horizontal");
}

QVector<QRect> SpreadAlgorithm::calculateZones(const TilingParams& params) const
{
    const int windowCount = params.windowCount;
    const auto& screenGeometry = params.screenGeometry;
    const int innerGap = params.innerGap;
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

    // splitRatio controls window width as a fraction of per-slot width
    const qreal widthFraction = params.state ? qBound(0.3, params.state->splitRatio(), 1.0) : 0.8;

    // Divide available width into equal slots
    const int totalGapSpace = (windowCount - 1) * innerGap;
    const int slotWidth = (area.width() - totalGapSpace) / windowCount;
    const int winWidth = std::max(50, qRound(slotWidth * widthFraction));

    // splitRatio also controls height fraction (windows are vertically centered)
    const int winHeight = std::max(50, qRound(area.height() * widthFraction));
    const int yOffset = (area.height() - winHeight) / 2;

    for (int i = 0; i < windowCount; ++i) {
        const int slotX = area.x() + i * (slotWidth + innerGap);
        // Center window within its slot
        const int x = slotX + (slotWidth - winWidth) / 2;
        const int y = area.y() + yOffset;
        zones.append(QRect(x, y, winWidth, winHeight));
    }

    return zones;
}

} // namespace PlasmaZones
