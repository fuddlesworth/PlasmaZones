// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "../TilingAlgorithm.h"

namespace PlasmaZones {

/**
 * @brief Dwindle tiling algorithm
 *
 * Recursively subdivides space using alternating vertical/horizontal splits.
 * Each window takes the left/top portion of the remaining area, with the
 * remainder shifting right/down. This matches the dwindle layout used by
 * i3, bspwm, and Hyprland.
 *
 * Layout example (5 windows, ratio=0.5):
 * ```
 * +----------+---------+
 * |          |    2    |
 * |    1     +----+----+
 * |          | 3  | 4  |
 * |          |    +----+
 * |          |    | 5  |
 * +----------+----+----+
 * ```
 *
 * Features:
 * - Dwindle subdivision (alternating vertical/horizontal)
 * - Configurable split ratio (default: 0.5)
 * - First window gets largest area
 * - Works well with any number of windows
 * - Predictable, consistent layout behavior
 */
class PLASMAZONES_EXPORT DwindleAlgorithm : public TilingAlgorithm
{
    Q_OBJECT

public:
    explicit DwindleAlgorithm(QObject *parent = nullptr);
    ~DwindleAlgorithm() override = default;

    // TilingAlgorithm interface
    QString name() const override;
    QString description() const override;
    QString icon() const noexcept override;

    QVector<QRect> calculateZones(const TilingParams &params) const override;

    bool supportsMasterCount() const noexcept override { return false; }
    bool supportsSplitRatio() const noexcept override { return true; }
    qreal defaultSplitRatio() const noexcept override { return 0.5; }
    int defaultMaxWindows() const noexcept override { return 5; }
};

} // namespace PlasmaZones
