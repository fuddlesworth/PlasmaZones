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
    explicit ThreeColumnAlgorithm(QObject* parent = nullptr);
    ~ThreeColumnAlgorithm() override = default;

    // TilingAlgorithm interface
    QString name() const override;
    QString description() const override;

    QVector<QRect> calculateZones(const TilingParams& params) const override;

    // Master is in center (index 0 in our output, but conceptually center)
    int masterZoneIndex() const override
    {
        return 0;
    }

    // Supports split ratio (center column width) but not master count
    bool supportsMasterCount() const override
    {
        return false;
    }
    bool supportsSplitRatio() const override
    {
        return true;
    }
    qreal defaultSplitRatio() const override
    {
        return 0.5;
    } // Center gets 50%
    bool centerLayout() const noexcept override
    {
        return true;
    }

    // Degrades gracefully to 1-2 columns for fewer windows
    int minimumWindows() const override
    {
        return 1;
    }
    int defaultMaxWindows() const override
    {
        return 5;
    }
};

} // namespace PlasmaZones
