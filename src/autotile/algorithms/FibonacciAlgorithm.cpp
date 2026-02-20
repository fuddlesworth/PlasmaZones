// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "FibonacciAlgorithm.h"
#include "../AlgorithmRegistry.h"
#include "../TilingState.h"
#include "core/constants.h"
#include <KLocalizedString>
#include <cmath>

namespace PlasmaZones {

using namespace AutotileDefaults;

// Self-registration: Fibonacci provides dwindle layout (priority 35)
namespace {
AlgorithmRegistrar<FibonacciAlgorithm> s_fibonacciRegistrar(DBus::AutotileAlgorithm::Fibonacci, 35);
}

FibonacciAlgorithm::FibonacciAlgorithm(QObject *parent)
    : TilingAlgorithm(parent)
{
}

QString FibonacciAlgorithm::name() const
{
    return i18n("Fibonacci");
}

QString FibonacciAlgorithm::description() const
{
    return i18n("Dwindle subdivision with alternating splits");
}

QString FibonacciAlgorithm::icon() const noexcept
{
    return QStringLiteral("shape-spiral");
}

QVector<QRect> FibonacciAlgorithm::calculateZones(const TilingParams &params) const
{
    const int windowCount = params.windowCount;
    const auto &screenGeometry = params.screenGeometry;
    const int innerGap = params.innerGap;
    const int outerGap = params.outerGap;
    const auto &minSizes = params.minSizes;

    QVector<QRect> zones;

    if (windowCount <= 0 || !screenGeometry.isValid() || !params.state) {
        return zones;
    }

    const auto &state = *params.state;

    const QRect area = innerRect(screenGeometry, outerGap);

    // Single window takes full available area
    if (windowCount == 1) {
        zones.append(area);
        return zones;
    }

    // Get split ratio from state
    const qreal splitRatio = std::clamp(state.splitRatio(), MinSplitRatio, MaxSplitRatio);

    // Precompute cumulative min dimensions for remaining windows at each split.
    // remainingMinWidth[i] = sum of minWidths for windows i..windowCount-1 + gaps
    // remainingMinHeight[i] = sum of minHeights for windows i..windowCount-1 + gaps
    // These are rough lower bounds used to prevent the current split from
    // starving remaining windows.
    QVector<int> remainingMinW(windowCount + 1, 0);
    QVector<int> remainingMinH(windowCount + 1, 0);
    if (!minSizes.isEmpty()) {
        for (int i = windowCount - 1; i >= 0; --i) {
            int mw = (i < minSizes.size()) ? std::max(0, minSizes[i].width()) : 0;
            int mh = (i < minSizes.size()) ? std::max(0, minSizes[i].height()) : 0;
            // For remaining windows beyond i, they need at least their min + gap
            remainingMinW[i] = mw + ((i < windowCount - 1 && remainingMinW[i + 1] > 0) ? innerGap + remainingMinW[i + 1] : 0);
            remainingMinH[i] = mh + ((i < windowCount - 1 && remainingMinH[i + 1] > 0) ? innerGap + remainingMinH[i + 1] : 0);
        }
    }

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
            // Graceful degradation: distribute remaining windows evenly with gaps
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
