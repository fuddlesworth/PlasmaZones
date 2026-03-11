// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "../TilingAlgorithm.h"

namespace PlasmaZones {

/**
 * @brief Stair tiling algorithm
 *
 * Arranges windows in a stepped staircase pattern where each window
 * occupies a non-overlapping step. Desktop-friendly layout that gives
 * each window a visible portion while maintaining spatial hierarchy.
 *
 * Layout examples:
 * ```
 * 2 windows:        3 windows:
 * +-----+-----+     +---+---+---+
 * |     |     |     |   |   |   |
 * |  1  |     |     | 1 |   |   |
 * |     |     |     |   |   |   |
 * +-----+     |     +---+   |   |
 *       |  2  |         | 2 |   |
 *       |     |         |   |   |
 *       |     |         +---+   |
 *       +-----+             | 3 |
 *                            +---+
 * ```
 *
 * Features:
 * - Non-overlapping stepped arrangement
 * - Each window gets equal step height and width
 * - Uses split ratio to control step size proportion
 * - No master/stack concept
 */
class PLASMAZONES_EXPORT StairAlgorithm : public TilingAlgorithm
{
    Q_OBJECT

public:
    explicit StairAlgorithm(QObject* parent = nullptr);
    ~StairAlgorithm() override = default;

    QString name() const override;
    QString description() const override;
    QString icon() const noexcept override;

    QVector<QRect> calculateZones(const TilingParams& params) const override;

    bool supportsMasterCount() const noexcept override
    {
        return false;
    }
    bool supportsSplitRatio() const noexcept override
    {
        return true;
    }
    qreal defaultSplitRatio() const noexcept override
    {
        return 0.5;
    }
    int defaultMaxWindows() const noexcept override
    {
        return 4;
    }
    bool producesOverlappingZones() const noexcept override
    {
        return true;
    }
};

} // namespace PlasmaZones
