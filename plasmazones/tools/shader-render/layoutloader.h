// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QColor>
#include <QRectF>
#include <QSize>
#include <QString>
#include <QVector>

namespace PlasmaZones::ShaderRender {

/**
 * @brief A single zone, ready to feed into ZoneUniformExtension.
 *
 * Coordinates are in normalized 0-1 space, matching the layout JSON's
 * relativeGeometry. common.glsl's helpers (zoneRectPos, zoneRectSize)
 * multiply by iResolution themselves — so passing pixel-space rects would
 * land off-screen. The shader sees these via the zoneRects[] /
 * zoneFillColors[] / zoneBorderColors[] / zoneParams[] UBO arrays — same
 * layout the daemon uses, so the preview matches what a user gets on a real
 * overlay.
 */
struct Zone
{
    QRectF rect; ///< Normalized 0-1 (NOT pixel-space).
    QColor fillColor;
    QColor borderColor;
    qreal borderWidth = 1.5;
    qreal borderRadius = 8.0;
    int zoneNumber = 1;
    /// Initial highlight state; the renderer's frame loop (Renderer::render)
    /// cycles this per slice so each zone gets a turn at the active state and
    /// the dormant state is also visible in the clip.
    bool isHighlighted = false;
};

/**
 * @brief Parse data/layouts/<id>.json into normalized 0-1 zones.
 *
 * Layout JSON stores zones in normalized [0,1] coords (so the same layout
 * works at any monitor size). Coordinates pass through unchanged — the
 * shaders' common.glsl helpers handle the multiplication by iResolution.
 * Optionally tints with the layout's per-zone fill / border colors if
 * present (otherwise a default brand cycle).
 *
 * The @p resolution parameter is unused by the loader itself (kept for API
 * stability); consumers that need pixel-space rects multiply by the render
 * resolution at the call site.
 */
bool loadLayoutZones(const QString& layoutPath, const QSize& resolution, QVector<Zone>& outZones);

} // namespace PlasmaZones::ShaderRender
