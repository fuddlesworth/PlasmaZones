// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QColor>
#include <QRect>
#include <QVector>

namespace KWin {
class RenderTarget;
class RenderViewport;
}

namespace PlasmaZones {

/**
 * @brief Draws colored borders around borderless autotiled windows.
 *
 * When autotile hides title bars, windows are inset by borderWidth on each side.
 * This renderer fills the gap between the original tile zone and the inset window
 * frame with colored rectangles using KWin's GL API.
 *
 * Call from paintScreen (after effects->paintScreen) to draw all borders at once.
 */
class AutotileBorderRenderer
{
public:
    /**
     * @brief Draw colored border frames for all provided zone geometries.
     *
     * Each zone geometry represents the original tile zone. The border fills the
     * gap between the zone edge and the inset window frame (borderWidth on each side).
     *
     * @param renderTarget  KWin render target (for colorspace)
     * @param viewport      KWin render viewport (for scale and projection)
     * @param zoneGeometries  Original tile zone geometries to draw borders around
     * @param borderWidth   Border thickness in logical pixels
     * @param borderColor   Border color (alpha-blended)
     */
    void drawBorders(const KWin::RenderTarget& renderTarget,
                     const KWin::RenderViewport& viewport,
                     const QVector<QRect>& zoneGeometries,
                     int borderWidth,
                     const QColor& borderColor);
};

} // namespace PlasmaZones
