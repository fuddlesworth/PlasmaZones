// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "../TilingAlgorithm.h"

namespace PlasmaZones {

/**
 * @brief Grid tiling algorithm
 *
 * Equal-sized NxM grid layout where rows and columns are auto-calculated
 * to keep cells as square as possible. No master/stack concept.
 *
 * Layout examples:
 * ```
 * 1 window:    4 windows:   5 windows:     9 windows:
 * +----------+ +----+----+  +---+---+---+  +---+---+---+
 * |          | |    |    |  |   |   |   |  | 1 | 2 | 3 |
 * |    1     | | 1  | 2  |  | 1 | 2 | 3 |  +---+---+---+
 * |          | +----+----+  +---+---+---+  | 4 | 5 | 6 |
 * +----------+ | 3  | 4  |  |  4  |  5  |  +---+---+---+
 *              +----+----+  +---+---+---+  | 7 | 8 | 9 |
 *                                          +---+---+---+
 * ```
 *
 * Features:
 * - Auto-calculated rows x cols for squarest cells
 * - Last row may have fewer windows spanning remaining width
 * - No master/ratio support (all windows equal)
 */
class PLASMAZONES_EXPORT GridAlgorithm : public TilingAlgorithm
{
    Q_OBJECT

public:
    explicit GridAlgorithm(QObject* parent = nullptr);
    ~GridAlgorithm() override = default;

    // TilingAlgorithm interface
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
        return false;
    }
    int defaultMaxWindows() const noexcept override
    {
        return 9;
    }
};

} // namespace PlasmaZones
