// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "FibonacciAlgorithm.h"
#include "../AlgorithmRegistry.h"
#include "../TilingState.h"
#include "core/constants.h"
#include <cmath>

namespace PlasmaZones {

using namespace AutotileDefaults;

// Self-registration: Fibonacci provides golden-ratio spiral layout (priority 35)
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
    return tr("Spiral subdivision inspired by golden ratio");
}

QString FibonacciAlgorithm::icon() const noexcept
{
    return QStringLiteral("shape-spiral");
}

FibonacciAlgorithm::SplitDirection FibonacciAlgorithm::nextDirection(SplitDirection current)
{
    switch (current) {
        case SplitDirection::Right: return SplitDirection::Down;
        case SplitDirection::Down:  return SplitDirection::Left;
        case SplitDirection::Left:  return SplitDirection::Up;
        case SplitDirection::Up:    return SplitDirection::Right;
    }
    return SplitDirection::Right; // Unreachable, but satisfies compiler
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

    // Get split ratio from state (default to golden ratio)
    const qreal splitRatio = std::clamp(state.splitRatio(), MinSplitRatio, MaxSplitRatio);

    // Start with full screen as the "remaining" area
    QRect remaining = screenGeometry;
    SplitDirection direction = SplitDirection::Right;

    for (int i = 0; i < windowCount; ++i) {
        // If remaining area is too small to split further, assign all of it
        if (i == windowCount - 1
            || remaining.width() < MinZoneSizePx || remaining.height() < MinZoneSizePx) {
            zones.append(remaining);
            // Graceful degradation: when remaining area is too small to split,
            // distribute remaining windows evenly within it (columns or rows)
            // rather than stacking them identically (which causes overlapping).
            const int leftover = windowCount - i - 1;
            if (leftover > 0) {
                if (remaining.width() >= remaining.height()) {
                    // Split horizontally into equal columns
                    QVector<int> widths = distributeEvenly(remaining.width(), leftover + 1);
                    // Replace the zone we just added with the first portion
                    zones.last() = QRect(remaining.x(), remaining.y(), widths[0], remaining.height());
                    int x = remaining.x() + widths[0];
                    for (int j = 1; j <= leftover; ++j) {
                        zones.append(QRect(x, remaining.y(), widths[j], remaining.height()));
                        x += widths[j];
                    }
                } else {
                    // Split vertically into equal rows
                    QVector<int> heights = distributeEvenly(remaining.height(), leftover + 1);
                    zones.last() = QRect(remaining.x(), remaining.y(), remaining.width(), heights[0]);
                    int y = remaining.y() + heights[0];
                    for (int j = 1; j <= leftover; ++j) {
                        zones.append(QRect(remaining.x(), y, remaining.width(), heights[j]));
                        y += heights[j];
                    }
                }
            }
            break;
        } else {
            // Split remaining area and assign portion to current window
            QRect windowZone;

            switch (direction) {
                case SplitDirection::Right: {
                    // Split vertically, current window on left
                    const int splitX = remaining.x() + static_cast<int>(remaining.width() * splitRatio);
                    windowZone = QRect(remaining.x(), remaining.y(),
                                       splitX - remaining.x(), remaining.height());
                    remaining = QRect(splitX, remaining.y(),
                                      remaining.right() - splitX + 1, remaining.height());
                    break;
                }
                case SplitDirection::Down: {
                    // Split horizontally, current window on top
                    const int splitY = remaining.y() + static_cast<int>(remaining.height() * splitRatio);
                    windowZone = QRect(remaining.x(), remaining.y(),
                                       remaining.width(), splitY - remaining.y());
                    remaining = QRect(remaining.x(), splitY,
                                      remaining.width(), remaining.bottom() - splitY + 1);
                    break;
                }
                case SplitDirection::Left: {
                    // Split vertically, current window on right
                    const int splitX = remaining.x() + static_cast<int>(remaining.width() * (1.0 - splitRatio));
                    windowZone = QRect(splitX, remaining.y(),
                                       remaining.right() - splitX + 1, remaining.height());
                    remaining = QRect(remaining.x(), remaining.y(),
                                      splitX - remaining.x(), remaining.height());
                    break;
                }
                case SplitDirection::Up: {
                    // Split horizontally, current window on bottom
                    const int splitY = remaining.y() + static_cast<int>(remaining.height() * (1.0 - splitRatio));
                    windowZone = QRect(remaining.x(), splitY,
                                       remaining.width(), remaining.bottom() - splitY + 1);
                    remaining = QRect(remaining.x(), remaining.y(),
                                      remaining.width(), splitY - remaining.y());
                    break;
                }
            }

            zones.append(windowZone);
            direction = nextDirection(direction);
        }
    }

    return zones;
}

} // namespace PlasmaZones
