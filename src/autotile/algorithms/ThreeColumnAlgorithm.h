// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "../TilingAlgorithm.h"

namespace PlasmaZones {

/**
 * @brief Three Column tiling algorithm
 *
 * Center master layout with side columns for secondary windows.
 * The master window occupies the center column, while stack windows
 * are distributed between left and right columns.
 *
 * Layout examples:
 * ```
 * 1 window:         2 windows:        3 windows:
 * +---------------+ +-------+-------+ +----+-------+----+
 * |               | |       |       | |    |       |    |
 * |    CENTER     | |   1   |   2   | | 1  |   2   | 3  |
 * |               | |       |       | |    |(master)|    |
 * +---------------+ +-------+-------+ +----+-------+----+
 *
 * 4 windows:        5 windows:
 * +----+-------+----+ +----+-------+----+
 * | 1  |       |    | | 1  |       | 4  |
 * +----+   2   | 3  | +----+   3   +----+
 * |    |(master)|    | | 2  |(master)| 5  |
 * | 4  |       |    | +----+       +----+
 * +----+-------+----+ +----+-------+----+
 * ```
 *
 * Features:
 * - Center master column (configurable width via split ratio)
 * - Side columns split evenly between remaining windows
 * - Left column fills first, then right
 * - Minimum 3 windows for true three-column layout
 */
class PLASMAZONES_EXPORT ThreeColumnAlgorithm : public TilingAlgorithm
{
    Q_OBJECT

public:
    explicit ThreeColumnAlgorithm(QObject *parent = nullptr);
    ~ThreeColumnAlgorithm() override = default;

    // TilingAlgorithm interface
    QString name() const noexcept override;
    QString description() const override;
    QString icon() const noexcept override;

    QVector<QRect> calculateZones(int windowCount, const QRect &screenGeometry,
                                  const TilingState &state) const override;

    // Master is in center (index 0 in our output, but conceptually center)
    int masterZoneIndex() const noexcept override { return 0; }

    // Supports split ratio (center column width) but not master count
    bool supportsMasterCount() const noexcept override { return false; }
    bool supportsSplitRatio() const noexcept override { return true; }
    qreal defaultSplitRatio() const noexcept override { return 0.5; } // Center gets 50%

    // Need at least 3 windows for true three-column layout
    int minimumWindows() const noexcept override { return 1; }
    int defaultMaxWindows() const noexcept override { return 5; }
};

} // namespace PlasmaZones
