// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "../TilingAlgorithm.h"

namespace PlasmaZones {

/**
 * @brief Centered Master tiling algorithm
 *
 * Master windows in center column with stacks wrapping both sides.
 * Unlike ThreeColumn (which always has exactly 1 center window),
 * CenteredMaster supports multiple masters stacked vertically in
 * the center column.
 *
 * Layout examples:
 * ```
 * 1 window:         2 windows:        3 windows:
 * +---------------+ +-------+-------+ +----+-------+----+
 * |               | |       |       | |    |       |    |
 * |    MASTER     | |   M   |   S1  | | S1 |   M   | S2 |
 * |               | |       |       | |    |       |    |
 * +---------------+ +-------+-------+ +----+-------+----+
 *
 * 5 windows (masterCount=2):
 * +----+-------+----+
 * | S1 |  M1   | S2 |
 * +----+-------+----+
 * | S3 |  M2   |    |
 * +----+-------+----+
 * ```
 *
 * Features:
 * - Multiple masters stacked vertically in center
 * - Stack windows interleaved left and right
 * - Configurable center width via split ratio
 * - Single window uses full screen
 */
class PLASMAZONES_EXPORT CenteredMasterAlgorithm : public TilingAlgorithm
{
    Q_OBJECT

public:
    explicit CenteredMasterAlgorithm(QObject* parent = nullptr);
    ~CenteredMasterAlgorithm() override = default;

    // TilingAlgorithm interface
    QString name() const override;
    QString description() const override;

    QVector<QRect> calculateZones(const TilingParams& params) const override;

    int masterZoneIndex() const override
    {
        return 0;
    }
    bool supportsMasterCount() const override
    {
        return true;
    }
    bool supportsSplitRatio() const override
    {
        return true;
    }
    qreal defaultSplitRatio() const override
    {
        return 0.5;
    }
    bool centerLayout() const override
    {
        return true;
    }
    int defaultMaxWindows() const override
    {
        return 7;
    }
};

} // namespace PlasmaZones
