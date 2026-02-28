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

SpiralAlgorithm::SpiralAlgorithm(QObject *parent)
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

QVector<QRect> SpiralAlgorithm::calculateZones(const TilingParams &params) const
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

    // Precompute cumulative min dimensions for remaining windows at each split.
    QVector<int> remainingMinW(windowCount + 1, 0);
    QVector<int> remainingMinH(windowCount + 1, 0);
    if (!minSizes.isEmpty()) {
        for (int i = windowCount - 1; i >= 0; --i) {
            int mw = (i < minSizes.size()) ? std::max(0, minSizes[i].width()) : 0;
            int mh = (i < minSizes.size()) ? std::max(0, minSizes[i].height()) : 0;
            remainingMinW[i] = mw + ((i < windowCount - 1 && remainingMinW[i + 1] > 0) ? innerGap + remainingMinW[i + 1] : 0);
            remainingMinH[i] = mh + ((i < windowCount - 1 && remainingMinH[i + 1] > 0) ? innerGap + remainingMinH[i + 1] : 0);
        }
    }

    // Spiral pattern: rotates through 4 directions.
    //   0: Right  — split vertical,   window=left,   remaining=right
    //   1: Down   — split horizontal, window=top,    remaining=bottom
    //   2: Left   — split vertical,   window=right,  remaining=left
    //   3: Up     — split horizontal, window=bottom,  remaining=top
    QRect remaining = area;

    for (int i = 0; i < windowCount; ++i) {
        // Last window or remaining area too small — assign all of it
        if (i == windowCount - 1
            || remaining.width() < MinZoneSizePx || remaining.height() < MinZoneSizePx) {
            zones.append(remaining);
            // Graceful degradation: distribute remaining windows evenly
            const int leftover = windowCount - i - 1;
            if (leftover > 0) {
                if (remaining.width() >= remaining.height()) {
                    const int maxFit = std::max(1, remaining.width() / MinZoneSizePx);
                    const int fitCount = std::min(leftover + 1, maxFit);
                    QVector<int> widths = distributeWithGaps(remaining.width(), fitCount, innerGap);
                    zones.last() = QRect(remaining.x(), remaining.y(), widths[0], remaining.height());
                    int x = remaining.x() + widths[0] + innerGap;
                    for (int j = 1; j < fitCount; ++j) {
                        zones.append(QRect(x, remaining.y(), widths[j], remaining.height()));
                        x += widths[j] + innerGap;
                    }
                    for (int j = fitCount; j <= leftover; ++j) {
                        zones.append(zones.last());
                    }
                } else {
                    const int maxFit = std::max(1, remaining.height() / MinZoneSizePx);
                    const int fitCount = std::min(leftover + 1, maxFit);
                    QVector<int> heights = distributeWithGaps(remaining.height(), fitCount, innerGap);
                    zones.last() = QRect(remaining.x(), remaining.y(), remaining.width(), heights[0]);
                    int y = remaining.y() + heights[0] + innerGap;
                    for (int j = 1; j < fitCount; ++j) {
                        zones.append(QRect(remaining.x(), y, remaining.width(), heights[j]));
                        y += heights[j] + innerGap;
                    }
                    for (int j = fitCount; j <= leftover; ++j) {
                        zones.append(zones.last());
                    }
                }
            }
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
                windowZone = QRect(remaining.x(), remaining.y(),
                                   windowWidth, remaining.height());
                remaining = QRect(remaining.x() + windowWidth + innerGap, remaining.y(),
                                  otherWidth, remaining.height());
            } else {
                // Left: window=right, remaining=left
                windowZone = QRect(remaining.x() + otherWidth + innerGap, remaining.y(),
                                   windowWidth, remaining.height());
                remaining = QRect(remaining.x(), remaining.y(),
                                  otherWidth, remaining.height());
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
                windowZone = QRect(remaining.x(), remaining.y(),
                                   remaining.width(), windowHeight);
                remaining = QRect(remaining.x(), remaining.y() + windowHeight + innerGap,
                                  remaining.width(), otherHeight);
            } else {
                // Up: window=bottom, remaining=top
                windowZone = QRect(remaining.x(), remaining.y() + otherHeight + innerGap,
                                   remaining.width(), windowHeight);
                remaining = QRect(remaining.x(), remaining.y(),
                                  remaining.width(), otherHeight);
            }
        }

        zones.append(windowZone);
    }

    return zones;
}

} // namespace PlasmaZones
