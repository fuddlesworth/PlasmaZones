// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "FibonacciAlgorithm.h"
#include "../AlgorithmRegistry.h"
#include "../TilingState.h"
#include "core/constants.h"
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

QString FibonacciAlgorithm::name() const noexcept
{
    return QStringLiteral("Fibonacci");
}

QString FibonacciAlgorithm::description() const
{
    return tr("Dwindle subdivision with alternating splits");
}

QString FibonacciAlgorithm::icon() const noexcept
{
    return QStringLiteral("shape-spiral");
}

QVector<QRect> FibonacciAlgorithm::calculateZones(int windowCount, const QRect &screenGeometry,
                                                   const TilingState &state) const
{
    QVector<QRect> zones;

    if (windowCount <= 0 || !screenGeometry.isValid()) {
        return zones;
    }

    // Single window takes full screen
    if (windowCount == 1) {
        zones.append(screenGeometry);
        return zones;
    }

    // Get split ratio from state
    const qreal splitRatio = std::clamp(state.splitRatio(), MinSplitRatio, MaxSplitRatio);

    // Dwindle pattern: alternates vertical/horizontal splits.
    // Current window always takes the left/top portion; remaining area
    // shifts right/down. This matches i3/bspwm/Hyprland dwindle behavior
    // and the built-in manual Fibonacci layout.
    QRect remaining = screenGeometry;
    bool splitVertical = true; // Start with vertical (left/right) split

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
                    QVector<int> widths = distributeEvenly(remaining.width(), fitCount);
                    zones.last() = QRect(remaining.x(), remaining.y(), widths[0], remaining.height());
                    int x = remaining.x() + widths[0];
                    for (int j = 1; j < fitCount; ++j) {
                        zones.append(QRect(x, remaining.y(), widths[j], remaining.height()));
                        x += widths[j];
                    }
                    for (int j = fitCount; j <= leftover; ++j) {
                        zones.append(zones.last());
                    }
                } else {
                    const int maxFit = std::max(1, remaining.height() / MinZoneSizePx);
                    const int fitCount = std::min(leftover + 1, maxFit);
                    QVector<int> heights = distributeEvenly(remaining.height(), fitCount);
                    zones.last() = QRect(remaining.x(), remaining.y(), remaining.width(), heights[0]);
                    int y = remaining.y() + heights[0];
                    for (int j = 1; j < fitCount; ++j) {
                        zones.append(QRect(remaining.x(), y, remaining.width(), heights[j]));
                        y += heights[j];
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
                // Split left/right — window gets left portion
                const int splitX = remaining.x() + static_cast<int>(remaining.width() * splitRatio);
                windowZone = QRect(remaining.x(), remaining.y(),
                                   splitX - remaining.x(), remaining.height());
                remaining = QRect(splitX, remaining.y(),
                                  remaining.right() - splitX + 1, remaining.height());
            } else {
                // Split top/bottom — window gets top portion
                const int splitY = remaining.y() + static_cast<int>(remaining.height() * splitRatio);
                windowZone = QRect(remaining.x(), remaining.y(),
                                   remaining.width(), splitY - remaining.y());
                remaining = QRect(remaining.x(), splitY,
                                  remaining.width(), remaining.bottom() - splitY + 1);
            }

            zones.append(windowZone);
            splitVertical = !splitVertical; // Alternate direction
        }
    }

    return zones;
}

} // namespace PlasmaZones
