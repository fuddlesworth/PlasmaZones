// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "../TilingAlgorithm.h"

namespace PlasmaZones {

/**
 * @brief Wide (horizontal master-stack) tiling algorithm
 *
 * Master row on top, stack row on bottom. Like MasterStack but rotated 90
 * degrees: the split is horizontal instead of vertical.
 *
 * Layout examples:
 * ```
 * 1 window:         2 windows:        3 windows (masterCount=2):
 * +---------------+ +---------------+ +-------+-------+
 * |               | |    MASTER     | |  M1   |  M2   |
 * |    MASTER     | |    (50%)      | |  (50% height) |
 * |               | +---------------+ +---+---+---+---+
 * +---------------+ |    STACK      | |     STACK     |
 *                   |    (50%)      | |     (50%)     |
 *                   +---------------+ +---------------+
 * ```
 *
 * Features:
 * - Adjustable split ratio (master height percentage)
 * - Multiple masters (laid out horizontally in top row)
 * - Stack windows divide horizontally in bottom row
 * - Single window uses full screen
 */
class PLASMAZONES_EXPORT WideAlgorithm : public TilingAlgorithm
{
    Q_OBJECT

public:
    explicit WideAlgorithm(QObject* parent = nullptr);
    ~WideAlgorithm() override = default;

    // TilingAlgorithm interface
    QString name() const override;
    QString description() const override;
    QString icon() const noexcept override;

    QVector<QRect> calculateZones(const TilingParams& params) const override;

    int masterZoneIndex() const noexcept override
    {
        return 0;
    }
    bool supportsMasterCount() const noexcept override
    {
        return true;
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
        return 5;
    }
};

} // namespace PlasmaZones
