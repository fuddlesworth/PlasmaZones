// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "../TilingAlgorithm.h"

namespace PlasmaZones {

/**
 * @brief Spiral tiling algorithm
 *
 * Recursively subdivides space by rotating through four directions:
 * right, down, left, up. This produces the characteristic spiral pattern
 * where windows wrap around a central point.
 *
 * Direction cycle per split:
 *   0: Right  — split vertical,   window=left,   remaining=right
 *   1: Down   — split horizontal, window=top,    remaining=bottom
 *   2: Left   — split vertical,   window=right,  remaining=left
 *   3: Up     — split horizontal, window=bottom,  remaining=top
 *
 * Layout example (5 windows, ratio=0.5):
 * ```
 * +----------+---------+
 * |          |    2    |
 * |    1     +----+----+
 * |          |    | 4  |
 * |          | 3  +----+
 * |          |    | 5  |
 * +----------+----+----+
 * ```
 *
 * Features:
 * - True 4-direction spiral subdivision
 * - Configurable split ratio (default: 0.5)
 * - First window gets largest area
 * - Works well with any number of windows
 */
class PLASMAZONES_EXPORT SpiralAlgorithm : public TilingAlgorithm
{
    Q_OBJECT

public:
    explicit SpiralAlgorithm(QObject *parent = nullptr);
    ~SpiralAlgorithm() override = default;

    // TilingAlgorithm interface
    QString name() const override;
    QString description() const override;
    QString icon() const noexcept override;

    QVector<QRect> calculateZones(const TilingParams &params) const override;

    bool supportsMasterCount() const noexcept override { return false; }
    bool supportsSplitRatio() const noexcept override { return true; }
    qreal defaultSplitRatio() const noexcept override { return 0.5; }
    int defaultMaxWindows() const noexcept override { return 5; }
};

} // namespace PlasmaZones
