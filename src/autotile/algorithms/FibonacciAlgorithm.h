// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "../TilingAlgorithm.h"

namespace PlasmaZones {

/**
 * @brief Fibonacci (Spiral) tiling algorithm
 *
 * Recursively subdivides space with each window taking a portion of the
 * remaining area, creating a spiral pattern inspired by the golden ratio.
 * Each split alternates direction (right, down, left, up) creating a
 * visually pleasing spiral arrangement.
 *
 * Layout example (5 windows):
 * ```
 * +-------------+--------+
 * |             |   2    |
 * |      1      +----+---+
 * |             | 3  | 4 |
 * |             +----+---+
 * |             |   5    |
 * +-------------+--------+
 * ```
 *
 * Features:
 * - Spiral subdivision pattern
 * - Configurable split ratio (default: golden ratio 0.618)
 * - First window gets largest area
 * - Works well with any number of windows
 * - Aesthetically pleasing golden-ratio proportions
 */
class PLASMAZONES_EXPORT FibonacciAlgorithm : public TilingAlgorithm
{
    Q_OBJECT

public:
    explicit FibonacciAlgorithm(QObject *parent = nullptr);
    ~FibonacciAlgorithm() override = default;

    // TilingAlgorithm interface
    QString name() const noexcept override;
    QString description() const override;
    QString icon() const noexcept override;

    QVector<QRect> calculateZones(int windowCount, const QRect &screenGeometry,
                                  const TilingState &state) const override;

    // Fibonacci supports split ratio but not master count
    bool supportsMasterCount() const noexcept override { return false; }
    bool supportsSplitRatio() const noexcept override { return true; }
    qreal defaultSplitRatio() const noexcept override { return 0.618; } // Golden ratio
    int defaultMaxWindows() const noexcept override { return 5; }

private:
    /**
     * @brief Split direction for Fibonacci spiral
     */
    enum class SplitDirection {
        Right,  ///< Split vertically, new window on right
        Down,   ///< Split horizontally, new window on bottom
        Left,   ///< Split vertically, new window on left
        Up      ///< Split horizontally, new window on top
    };

    /**
     * @brief Get next direction in spiral sequence
     */
    static SplitDirection nextDirection(SplitDirection current);
};

} // namespace PlasmaZones
