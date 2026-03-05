// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "SpiralAlgorithm.h"
#include "../AlgorithmRegistry.h"
#include "../TilingState.h"
#include "core/constants.h"
#include <KLocalizedString>
#include <cmath>

namespace PlasmaZones {

using namespace AutotileDefaults;

// Self-registration: Spiral provides 4-direction rotation (alphabetical priority 90)
namespace {
AlgorithmRegistrar<SpiralAlgorithm> s_spiralRegistrar(DBus::AutotileAlgorithm::Spiral, 90);
}

SpiralAlgorithm::SpiralAlgorithm(QObject* parent)
    : TilingAlgorithm(parent)
{
}

QString SpiralAlgorithm::name() const
{
    return i18n("Spiral");
}

QString SpiralAlgorithm::description() const
{
    return i18n("Spiral subdivision rotating through four directions");
}

QString SpiralAlgorithm::icon() const noexcept
{
    return QStringLiteral("shape-spiral");
}

QVector<QRect> SpiralAlgorithm::calculateZones(const TilingParams& params) const
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

    const QRect area = innerRect(screenGeometry, outerGaps);

    // Single window takes full available area
    if (windowCount == 1) {
        zones.append(area);
        return zones;
    }

    // Read split ratio from TilingState (user-adjustable via slider)
    const qreal splitRatio = std::clamp(params.state->splitRatio(), MinSplitRatio, MaxSplitRatio);

    // Precompute direction-aware cumulative min dimensions for remaining windows.
    // Spiral rotates through 4 directions but splitV = (i%2==0), same as Dwindle.
    const auto cumMinDims = computeAlternatingCumulativeMinDims(windowCount, minSizes, innerGap);
    const auto& remainingMinW = cumMinDims.minW;
    const auto& remainingMinH = cumMinDims.minH;

    // Spiral pattern: rotates through 4 directions.
    //   0: Right  — split vertical,   window=left,   remaining=right
    //   1: Down   — split horizontal, window=top,    remaining=bottom
    //   2: Left   — split vertical,   window=right,  remaining=left
    //   3: Up     — split horizontal, window=bottom,  remaining=top
    QRect remaining = area;

    for (int i = 0; i < windowCount; ++i) {
        // Last window or remaining area too small — assign all of it
        if (i == windowCount - 1 || remaining.width() < MinZoneSizePx || remaining.height() < MinZoneSizePx) {
            zones.append(remaining);
            appendGracefulDegradation(zones, remaining, windowCount - i - 1, innerGap);
            break;
        }

        const int dir = i % 4;
        QRect windowZone;

        if (dir == 0 || dir == 2) {
            // Vertical split
            const int contentWidth = remaining.width() - innerGap;
            if (contentWidth <= 0) {
                zones.append(remaining);
                for (int j = i + 1; j < windowCount; ++j) {
                    zones.append(remaining);
                }
                break;
            }
            int windowWidth = static_cast<int>(contentWidth * splitRatio);

            // Clamp for min sizes
            if (!minSizes.isEmpty() && i < minSizes.size() && minSizes[i].width() > 0) {
                windowWidth = std::max(windowWidth, minSizes[i].width());
            }
            if (!minSizes.isEmpty() && remainingMinW[i + 1] > 0) {
                windowWidth = std::min(windowWidth, contentWidth - remainingMinW[i + 1]);
            }
            windowWidth = std::clamp(windowWidth, 1, contentWidth - 1);

            const int otherWidth = contentWidth - windowWidth;

            if (dir == 0) {
                // Right: window=left, remaining=right
                windowZone = QRect(remaining.x(), remaining.y(), windowWidth, remaining.height());
                remaining =
                    QRect(remaining.x() + windowWidth + innerGap, remaining.y(), otherWidth, remaining.height());
            } else {
                // Left: window=right, remaining=left
                windowZone =
                    QRect(remaining.x() + otherWidth + innerGap, remaining.y(), windowWidth, remaining.height());
                remaining = QRect(remaining.x(), remaining.y(), otherWidth, remaining.height());
            }
        } else {
            // Horizontal split (dir == 1 or dir == 3)
            const int contentHeight = remaining.height() - innerGap;
            if (contentHeight <= 0) {
                zones.append(remaining);
                for (int j = i + 1; j < windowCount; ++j) {
                    zones.append(remaining);
                }
                break;
            }
            int windowHeight = static_cast<int>(contentHeight * splitRatio);

            // Clamp for min sizes
            if (!minSizes.isEmpty() && i < minSizes.size() && minSizes[i].height() > 0) {
                windowHeight = std::max(windowHeight, minSizes[i].height());
            }
            if (!minSizes.isEmpty() && remainingMinH[i + 1] > 0) {
                windowHeight = std::min(windowHeight, contentHeight - remainingMinH[i + 1]);
            }
            windowHeight = std::clamp(windowHeight, 1, contentHeight - 1);

            const int otherHeight = contentHeight - windowHeight;

            if (dir == 1) {
                // Down: window=top, remaining=bottom
                windowZone = QRect(remaining.x(), remaining.y(), remaining.width(), windowHeight);
                remaining =
                    QRect(remaining.x(), remaining.y() + windowHeight + innerGap, remaining.width(), otherHeight);
            } else {
                // Up: window=bottom, remaining=top
                windowZone =
                    QRect(remaining.x(), remaining.y() + otherHeight + innerGap, remaining.width(), windowHeight);
                remaining = QRect(remaining.x(), remaining.y(), remaining.width(), otherHeight);
            }
        }

        zones.append(windowZone);
    }

    return zones;
}

} // namespace PlasmaZones
