// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "../TilingAlgorithm.h"

namespace PlasmaZones {

/**
 * @brief Rows tiling algorithm
 *
 * Simple layout that divides the screen into equal-height horizontal rows,
 * one per window. This is the horizontal counterpart to the Columns algorithm.
 *
 * Layout examples:
 * ```
 * 1 window:    2 windows:   3 windows:   4 windows:
 * +----------+ +----------+ +----------+ +----------+
 * |          | |    1     | |    1     | |    1     |
 * |    1     | +----------+ +----------+ +----------+
 * |          | |    2     | |    2     | |    2     |
 * +----------+ +----------+ +----------+ +----------+
 *                           |    3     | |    3     |
 *                           +----------+ +----------+
 *                                        |    4     |
 *                                        +----------+
 * ```
 *
 * Features:
 * - Equal-height rows for any window count
 * - Single window uses full screen
 * - No master/stack concept (all windows equal)
 */
class PLASMAZONES_EXPORT RowsAlgorithm : public TilingAlgorithm
{
    Q_OBJECT

public:
    explicit RowsAlgorithm(QObject *parent = nullptr);
    ~RowsAlgorithm() override = default;

    // TilingAlgorithm interface
    QString name() const override;
    QString description() const override;
    QString icon() const noexcept override;

    QVector<QRect> calculateZones(const TilingParams &params) const override;

    // Rows doesn't support master count or split ratio
    bool supportsMasterCount() const noexcept override { return false; }
    bool supportsSplitRatio() const noexcept override { return false; }
    int defaultMaxWindows() const noexcept override { return 4; }
};

} // namespace PlasmaZones
