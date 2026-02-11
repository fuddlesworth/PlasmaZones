// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <plasmazones_rendering_export.h>
#include <QColor>
#include <QFont>
#include <QImage>
#include <QSize>
#include <QVariantList>

namespace PlasmaZones {

/**
 * @brief Renders zone numbers to a QImage for shader texture pass
 *
 * Produces a full-overlay-sized image with zone numbers drawn at zone rect
 * positions. Uses QPainter for text rendering with outline for visibility.
 * Output is premultiplied alpha for correct shader compositing.
 */
class PLASMAZONES_RENDERING_EXPORT ZoneLabelTextureBuilder
{
public:
    /**
     * @brief Build a labels texture from zone data
     * @param zones Zone data (QVariantList of maps with x, y, width, height, zoneNumber)
     * @param size Overlay size in pixels (texture dimensions)
     * @param labelFontColor Text color for zone labels
     * @param showNumbers Whether to draw numbers (false returns null)
     * @param backgroundColor Background color for outline contrast (default Qt::black)
     * @param fontFamily Font family name (empty = system default)
     * @param fontSizeScale Multiplier on auto-calculated font size (default 1.0)
     * @param fontWeight QFont::Weight value 100-900 (default QFont::Bold)
     * @param fontItalic Whether to use italic style (default false)
     * @param fontUnderline Whether to underline text (default false)
     * @param fontStrikeout Whether to strike out text (default false)
     * @return QImage (Format_ARGB32_Premultiplied) or null if showNumbers=false or no zones
     */
    static QImage build(const QVariantList& zones,
                        const QSize& size,
                        const QColor& labelFontColor,
                        bool showNumbers,
                        const QColor& backgroundColor = Qt::black,
                        const QString& fontFamily = QString(),
                        qreal fontSizeScale = 1.0,
                        int fontWeight = QFont::Bold,
                        bool fontItalic = false,
                        bool fontUnderline = false,
                        bool fontStrikeout = false);
};

} // namespace PlasmaZones
