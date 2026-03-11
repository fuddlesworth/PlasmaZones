// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "../TilingAlgorithm.h"

namespace PlasmaZones {

/**
 * @brief Cascade tiling algorithm
 *
 * Arranges windows in an overlapping cascade pattern, each offset
 * diagonally from the previous. Desktop-friendly layout that keeps
 * all title bars visible while maximizing usable window area.
 *
 * Layout examples:
 * ```
 * 1 window:         2 windows:        3 windows:
 * +----------+      +----------+      +----------+
 * |          |      | +--------+-+    | +--------+-+
 * |    1     |      | |        | |    | | +------+-+-+
 * |          |      | |   2    | |    | | |      | | |
 * +----------+      +-+--------+ |    | | |  3   | | |
 *                     |    1     |    +-+-+-------+ | |
 *                     +----------+      | |   2    | |
 *                                       +-+--------+ |
 *                                         |    1     |
 *                                         +----------+
 * ```
 *
 * Features:
 * - Overlapping windows with diagonal offset
 * - All title bars remain visible
 * - No master/stack concept
 * - Uses split ratio to control cascade offset proportion
 */
class PLASMAZONES_EXPORT CascadeAlgorithm : public TilingAlgorithm
{
    Q_OBJECT

public:
    explicit CascadeAlgorithm(QObject* parent = nullptr);
    ~CascadeAlgorithm() override = default;

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
        return 0.15;
    }
    int defaultMaxWindows() const noexcept override
    {
        return 5;
    }
    bool producesOverlappingZones() const noexcept override
    {
        return true;
    }
};

} // namespace PlasmaZones
