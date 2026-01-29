// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "../TilingAlgorithm.h"

namespace PlasmaZones {

/**
 * @brief Columns tiling algorithm
 *
 * Simple layout that divides the screen into equal-width vertical columns,
 * one per window. This is the simplest tiling algorithm.
 *
 * Layout examples:
 * ```
 * 1 window:    2 windows:   3 windows:   4 windows:
 * +----------+ +-----+----+ +---+---+---+ +--+--+--+--+
 * |          | |     |    | |   |   |   | |  |  |  |  |
 * |    1     | |  1  |  2 | | 1 | 2 | 3 | |1 |2 |3 |4 |
 * |          | |     |    | |   |   |   | |  |  |  |  |
 * +----------+ +-----+----+ +---+---+---+ +--+--+--+--+
 * ```
 *
 * Features:
 * - Equal-width columns for any window count
 * - Single window uses full screen
 * - No master/stack concept (all windows equal)
 */
class PLASMAZONES_EXPORT ColumnsAlgorithm : public TilingAlgorithm
{
    Q_OBJECT

public:
    explicit ColumnsAlgorithm(QObject *parent = nullptr);
    ~ColumnsAlgorithm() override = default;

    // TilingAlgorithm interface
    QString name() const noexcept override;
    QString description() const override;
    QString icon() const noexcept override;

    QVector<QRect> calculateZones(int windowCount, const QRect &screenGeometry,
                                  const TilingState &state) const override;

    // Columns doesn't support master count or split ratio
    bool supportsMasterCount() const noexcept override { return false; }
    bool supportsSplitRatio() const noexcept override { return false; }
};

} // namespace PlasmaZones
