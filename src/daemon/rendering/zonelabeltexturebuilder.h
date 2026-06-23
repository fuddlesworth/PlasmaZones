// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <plasmazones_rendering_export.h>
#include <PhosphorRendering/ZoneLabelTexture.h>
#include <QColor>
#include <QFont>
#include <QSize>
#include <QVariantList>

namespace PlasmaZones {

/**
 * @brief Renders zone numbers into a sparse glyph-tile payload for the shader
 *        texture pass.
 *
 * Each zone number is rendered into its own small ARGB32-premultiplied tile
 * (glyph bounds plus outline/decoration margin) positioned at the zone centre;
 * the render node composites the tiles into a screen-addressed texture at upload
 * time. This keeps only a few hundred KB of glyph pixels resident instead of a
 * full-overlay-sized (~99% transparent) image. Uses QPainter with an outline for
 * visibility; tiles are premultiplied alpha for correct compositing.
 */
class PLASMAZONES_RENDERING_EXPORT ZoneLabelTextureBuilder
{
public:
    /**
     * @brief Build the sparse zone-labels payload from zone data
     * @param zones PhosphorZones::Zone data (QVariantList of maps with x, y, width, height, zoneNumber)
     * @param size Overlay size in pixels (the screen-addressed texture dimensions the tiles map into)
     * @param labelFontColor Text color for zone labels
     * @param showNumbers Whether to draw numbers (false returns an empty payload)
     * @param backgroundColor Background color for outline contrast (default Qt::black)
     * @param fontFamily Font family name (empty = system default)
     * @param fontSizeScale Multiplier on auto-calculated font size (default 1.0)
     * @param fontWeight QFont::Weight value 100-900 (default QFont::Bold)
     * @param fontItalic Whether to use italic style (default false)
     * @param fontUnderline Whether to underline text (default false)
     * @param fontStrikeout Whether to strike out text (default false)
     * @return Sparse ZoneLabelTexture payload (empty if showNumbers=false or no zones)
     */
    static PhosphorRendering::ZoneLabelTexture build(const QVariantList& zones, const QSize& size,
                                                     const QColor& labelFontColor, bool showNumbers,
                                                     const QColor& backgroundColor = Qt::black,
                                                     const QString& fontFamily = QString(), qreal fontSizeScale = 1.0,
                                                     int fontWeight = QFont::Bold, bool fontItalic = false,
                                                     bool fontUnderline = false, bool fontStrikeout = false);
};

} // namespace PlasmaZones
