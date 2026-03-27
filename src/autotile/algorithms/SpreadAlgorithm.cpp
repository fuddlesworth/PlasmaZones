// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "SpreadAlgorithm.h"
#include "../AlgorithmRegistry.h"
#include "../TilingState.h"
#include "core/constants.h"
#include "pz_i18n.h"
#include <cmath>

namespace PlasmaZones {

namespace {
constexpr qreal SpreadMinRatio = 0.3;
constexpr qreal SpreadMaxRatio = 1.0;
AlgorithmRegistrar<SpreadAlgorithm> s_spreadRegistrar(DBus::AutotileAlgorithm::Spread, 140);
}

SpreadAlgorithm::SpreadAlgorithm(QObject* parent)
    : TilingAlgorithm(parent)
{
}

QString SpreadAlgorithm::name() const
{
    return PzI18n::tr("Spread");
}

QString SpreadAlgorithm::description() const
{
    return PzI18n::tr("Windows spread evenly across the screen");
}

QVector<QRect> SpreadAlgorithm::calculateZones(const TilingParams& params) const
{
    const int windowCount = params.windowCount;
    const auto& screenGeometry = params.screenGeometry;
    const int innerGap = params.innerGap;
    const auto& outerGaps = params.outerGaps;
    const auto& minSizes = params.minSizes;

    QVector<QRect> zones;

    if (windowCount <= 0 || !screenGeometry.isValid() || !params.state) {
        return zones;
    }

    const auto& state = *params.state;

    const QRect area = innerRect(screenGeometry, outerGaps);

    if (windowCount == 1) {
        zones.append(area);
        return zones;
    }

    // splitRatio controls window size as a fraction of per-slot dimensions
    const qreal widthFraction = qBound(SpreadMinRatio, state.splitRatio(), SpreadMaxRatio);

    // Extract per-window minimum sizes.
    // Slot minimums are scaled up by 1/widthFraction so the window minimum is
    // still met after the fraction is applied (slot * fraction >= minWidth).
    QVector<int> minWidths(windowCount, 0);
    QVector<int> slotMinWidths(windowCount, 0);
    QVector<int> minHeights(windowCount, 0);
    if (!minSizes.isEmpty()) {
        for (int i = 0; i < windowCount && i < minSizes.size(); ++i) {
            minWidths[i] = minSizes[i].width();
            slotMinWidths[i] = minWidths[i] > 0 ? static_cast<int>(std::ceil(minWidths[i] / widthFraction)) : 0;
            minHeights[i] = minSizes[i].height();
        }
    }

    // Distribute slot widths respecting scaled minimum sizes
    const QVector<int> slotWidths = slotMinWidths.isEmpty()
        ? distributeWithGaps(area.width(), windowCount, innerGap)
        : distributeWithMinSizes(area.width(), windowCount, innerGap, slotMinWidths);

    // splitRatio also controls height fraction (windows are vertically centered)
    const int baseHeight = std::max(50, qRound(area.height() * widthFraction));

    int currentX = area.x();
    for (int i = 0; i < windowCount; ++i) {
        const int slotW = slotWidths[i];
        // Window width: fraction of slot, but never smaller than min width
        int winWidth = std::max(50, qRound(slotW * widthFraction));
        if (minWidths[i] > 0) {
            winWidth = std::max(winWidth, minWidths[i]);
        }
        winWidth = std::min(winWidth, slotW); // don't exceed slot

        // Window height: never smaller than min height
        int winHeight = baseHeight;
        if (minHeights[i] > 0) {
            winHeight = std::max(winHeight, minHeights[i]);
        }
        winHeight = std::min(winHeight, area.height()); // don't exceed area

        // Center window within its slot
        const int x = currentX + (slotW - winWidth) / 2;
        const int yOffset = (area.height() - winHeight) / 2;
        const int y = area.y() + yOffset;
        zones.append(QRect(x, y, winWidth, winHeight));
        currentX += slotW + innerGap;
    }

    return zones;
}

} // namespace PlasmaZones
