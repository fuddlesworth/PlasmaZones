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
 * Coordinates are in pixel space (resolved from the layout JSON's
 * normalized 0-1 against the render resolution).  The shader sees
 * these via the zoneRects[]/zoneFillColors[]/zoneBorderColors[]/
 * zoneParams[] UBO arrays — same layout the daemon uses, so the
 * preview matches what a user gets on a real overlay.
 */
struct Zone
{
    QRectF rect;
    QColor fillColor;
    QColor borderColor;
    qreal borderWidth   = 1.5;
    qreal borderRadius  = 8.0;
    int zoneNumber      = 1;
    bool isHighlighted  = true;     ///< Set on every zone for previews — most
                                    ///< audio-reactive shaders keyframe off this.
};

/**
 * @brief Parse data/layouts/<id>.json and resolve the zones to
 * pixel-space rects against the requested render resolution.
 *
 * Layout JSON stores zones in normalized [0,1] coords (so the same
 * layout works at any monitor size).  Multiplied by the resolution
 * here, then optionally tinted with the layout's per-zone fill /
 * border colors if present (otherwise a default brand cycle).
 */
bool loadLayoutZones(const QString& layoutPath,
                     const QSize& resolution,
                     QVector<Zone>& outZones);

} // namespace PlasmaZones::ShaderRender
