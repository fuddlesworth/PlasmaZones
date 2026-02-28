// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "DwindleAlgorithm.h"
#include "../AlgorithmRegistry.h"
#include "../TilingState.h"
#include "core/constants.h"
#include <KLocalizedString>
#include <cmath>

namespace PlasmaZones {

using namespace AutotileDefaults;

// Self-registration: Dwindle provides alternating V/H subdivision (alphabetical priority 40)
namespace {
AlgorithmRegistrar<DwindleAlgorithm> s_dwindleRegistrar(DBus::AutotileAlgorithm::Dwindle, 40);
}

DwindleAlgorithm::DwindleAlgorithm(QObject *parent)
    : TilingAlgorithm(parent)
{
}

QString DwindleAlgorithm::name() const
{
    return i18n("Dwindle");
}

QString DwindleAlgorithm::description() const
{
    return i18n("Dwindle subdivision with alternating vertical/horizontal splits");
}

QString DwindleAlgorithm::icon() const noexcept
{
    return QStringLiteral("view-grid-symbolic");
}

QVector<QRect> DwindleAlgorithm::calculateZones(const TilingParams &params) const
{
    const int windowCount = params.windowCount;
    const auto &screenGeometry = params.screenGeometry;
    const int innerGap = params.innerGap;
    const auto &outerGaps = params.outerGaps;
    const auto &minSizes = params.minSizes;

    QVector<QRect> zones;

    if (windowCount <= 0 || !screenGeometry.isValid() || !params.state) {
        return zones;
    }

    const QRect area = innerRect(screenGeometry, outerGaps);

    // Single window takes full available area
    if (windowCount == 1) {
        zones.append(area);
        return zones;
    }

    // Read split ratio from TilingState (user-adjustable via slider)
    const qreal splitRatio = std::clamp(params.state->splitRatio(),
                                        MinSplitRatio, MaxSplitRatio);

    // Precompute direction-aware cumulative min dimensions for remaining windows.
    const auto cumMinDims = computeAlternatingCumulativeMinDims(windowCount, minSizes, innerGap);
    const auto &remainingMinW = cumMinDims.minW;
    const auto &remainingMinH = cumMinDims.minH;

    // Dwindle pattern: alternates vertical/horizontal splits.
    // Current window always takes the left/top portion; remaining area
    // shifts right/down. Each split deducts innerGap from the content space.
    QRect remaining = area;
    bool splitVertical = true; // Start with vertical (left/right) split

    for (int i = 0; i < windowCount; ++i) {
        // Last window or remaining area too small — assign all of it
        if (i == windowCount - 1
            || remaining.width() < MinZoneSizePx || remaining.height() < MinZoneSizePx
            || (splitVertical && remaining.width() <= innerGap)
            || (!splitVertical && remaining.height() <= innerGap)) {
            zones.append(remaining);
            appendGracefulDegradation(zones, remaining, windowCount - i - 1, innerGap);
            break;
        } else {
            QRect windowZone;

            if (splitVertical) {
                // Split left/right — window gets left portion, gap in between
                const int contentWidth = remaining.width() - innerGap;
                int windowWidth = static_cast<int>(contentWidth * splitRatio);

                // Clamp: current window gets at least its min width
                if (!minSizes.isEmpty() && i < minSizes.size() && minSizes[i].width() > 0) {
                    windowWidth = std::max(windowWidth, minSizes[i].width());
                }
                // Clamp: remaining windows need at least their combined min width
                if (!minSizes.isEmpty() && remainingMinW[i + 1] > 0) {
                    windowWidth = std::min(windowWidth, contentWidth - remainingMinW[i + 1]);
                }
                windowWidth = std::clamp(windowWidth, 1, contentWidth - 1);

                windowZone = QRect(remaining.x(), remaining.y(),
                                   windowWidth, remaining.height());
                remaining = QRect(remaining.x() + windowWidth + innerGap, remaining.y(),
                                  contentWidth - windowWidth, remaining.height());
            } else {
                // Split top/bottom — window gets top portion, gap in between
                const int contentHeight = remaining.height() - innerGap;
                int windowHeight = static_cast<int>(contentHeight * splitRatio);

                // Clamp: current window gets at least its min height
                if (!minSizes.isEmpty() && i < minSizes.size() && minSizes[i].height() > 0) {
                    windowHeight = std::max(windowHeight, minSizes[i].height());
                }
                // Clamp: remaining windows need at least their combined min height
                if (!minSizes.isEmpty() && remainingMinH[i + 1] > 0) {
                    windowHeight = std::min(windowHeight, contentHeight - remainingMinH[i + 1]);
                }
                windowHeight = std::clamp(windowHeight, 1, contentHeight - 1);

                windowZone = QRect(remaining.x(), remaining.y(),
                                   remaining.width(), windowHeight);
                remaining = QRect(remaining.x(), remaining.y() + windowHeight + innerGap,
                                  remaining.width(), contentHeight - windowHeight);
            }

            zones.append(windowZone);
            splitVertical = !splitVertical; // Alternate direction
        }
    }

    return zones;
}

} // namespace PlasmaZones
