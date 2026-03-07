// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "../TilingAlgorithm.h"

namespace PlasmaZones {

/**
 * @brief Spread tiling algorithm
 *
 * Arranges windows in a dispersed pattern across the screen, each
 * sized to a fraction of the screen with even spacing between them.
 * Desktop-friendly layout that keeps windows visible and accessible
 * without full tiling.
 *
 * Layout examples:
 * ```
 * 1 window:         2 windows:         3 windows:
 * +----------+      +----------+       +----------+
 * |+--------+|      |+----++----+|     |+--++--++--+|
 * ||        ||      ||    ||    ||     || 1|| 2|| 3||
 * ||   1    ||      || 1  || 2  ||     ||  ||  ||  ||
 * ||        ||      ||    ||    ||     |+--++--++--+|
 * |+--------+|      |+----++----+|     +----------+
 * +----------+      +----------+
 * ```
 *
 * Features:
 * - Windows sized to a fraction of available space
 * - Even horizontal distribution with vertical centering
 * - Split ratio controls window width fraction (of per-slot width)
 * - No master/stack concept
 */
class PLASMAZONES_EXPORT SpreadAlgorithm : public TilingAlgorithm
{
    Q_OBJECT

public:
    explicit SpreadAlgorithm(QObject* parent = nullptr);
    ~SpreadAlgorithm() override = default;

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
        return 0.8;
    }
    int defaultMaxWindows() const noexcept override
    {
        return 4;
    }
};

} // namespace PlasmaZones
