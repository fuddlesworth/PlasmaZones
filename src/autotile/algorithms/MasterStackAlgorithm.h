// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "../TilingAlgorithm.h"

namespace PlasmaZones {

/**
 * @brief Master-Stack tiling algorithm
 *
 * Classic tiling layout with one or more "master" windows taking a large
 * portion of the screen (typically left side, 55-60%), with remaining
 * windows stacked vertically on the right.
 *
 * Layout example (1 master, 3 stack):
 * ```
 * +------------------+--------+
 * |                  |   2    |
 * |     MASTER       |--------|
 * |     (60%)        |   3    |
 * |                  |--------|
 * |                  |   4    |
 * +------------------+--------+
 * ```
 *
 * Features:
 * - Adjustable split ratio (master width percentage)
 * - Multiple masters (stacked vertically in master area)
 * - Stack windows divide evenly
 * - Single window uses full screen
 */
class PLASMAZONES_EXPORT MasterStackAlgorithm : public TilingAlgorithm
{
    Q_OBJECT

public:
    explicit MasterStackAlgorithm(QObject *parent = nullptr);
    ~MasterStackAlgorithm() override = default;

    // TilingAlgorithm interface
    QString name() const override;
    QString description() const override;
    QString icon() const override;

    QVector<QRect> calculateZones(int windowCount, const QRect &screenGeometry,
                                  const TilingState &state) const override;

    int masterZoneIndex() const override;
    bool supportsMasterCount() const override;
    bool supportsSplitRatio() const override;
    qreal defaultSplitRatio() const override;
};

} // namespace PlasmaZones
