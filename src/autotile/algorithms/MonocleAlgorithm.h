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
 * All windows get the same geometry (full screen), and window
 * visibility is controlled by the autotiling engine based on
 * monocleHideOthers setting in AutotileConfig.
 *
 * Features:
 * - All windows use full screen geometry
 * - No master/stack concept
 * - No split ratio (all windows same size)
 * - Ideal for focused single-window workflow
 *
 * @see AutotileConfig::monocleHideOthers for visibility behavior
 * @see AutotileConfig::monocleShowTabs for tab bar display
 */
class PLASMAZONES_EXPORT MonocleAlgorithm : public TilingAlgorithm
{
    Q_OBJECT

public:
    explicit MonocleAlgorithm(QObject *parent = nullptr);
    ~MonocleAlgorithm() override = default;

    // TilingAlgorithm interface
    QString name() const noexcept override;
    QString description() const override;
    QString icon() const noexcept override;

    QVector<QRect> calculateZones(int windowCount, const QRect &screenGeometry,
                                  const TilingState &state) const override;

    // Monocle doesn't support master count or split ratio - all windows are fullscreen
    bool supportsMasterCount() const noexcept override { return false; }
    bool supportsSplitRatio() const noexcept override { return false; }
    int defaultMaxWindows() const noexcept override { return 10; }
};

} // namespace PlasmaZones
