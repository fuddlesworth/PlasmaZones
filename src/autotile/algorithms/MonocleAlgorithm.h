// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "../TilingAlgorithm.h"

namespace PlasmaZones {

/**
 * @brief Monocle tiling algorithm
 *
 * Single fullscreen window visible at a time. All windows occupy
 * the full screen area, stacked on top of each other. The focused
 * window is displayed, while others are either hidden (minimized)
 * or simply behind the focused window, depending on configuration.
 *
 * Layout:
 * ```
 * +------------------------+
 * |                        |
 * |     Focused Window     |
 * |      (fullscreen)      |
 * |                        |
 * +------------------------+
 * ```
 *
 * All windows get the same geometry (full screen area) and are stacked
 * on top of each other. KWin's stacking order controls visibility —
 * the focused window is naturally on top.
 *
 * Features:
 * - All windows use full screen geometry
 * - No master/stack concept
 * - No split ratio (all windows same size)
 * - Ideal for focused single-window workflow
 * - Use focusNext/focusPrevious to cycle through stacked windows
 */
class PLASMAZONES_EXPORT MonocleAlgorithm : public TilingAlgorithm
{
    Q_OBJECT

public:
    explicit MonocleAlgorithm(QObject* parent = nullptr);
    ~MonocleAlgorithm() override = default;

    // TilingAlgorithm interface
    QString name() const override;
    QString description() const override;
    QString icon() const noexcept override;

    QVector<QRect> calculateZones(const TilingParams& params) const override;

    // Monocle doesn't support master count or split ratio - all windows are fullscreen
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
        return 4;
    }
};

} // namespace PlasmaZones
