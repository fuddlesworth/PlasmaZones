// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QColor>
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
class ZoneLabelTextureBuilder
{
public:
    /**
     * @brief Build a labels texture from zone data
     * @param zones Zone data (QVariantList of maps with x, y, width, height, zoneNumber)
     * @param size Overlay size in pixels (texture dimensions)
     * @param numberColor Text color for zone numbers
     * @param showNumbers Whether to draw numbers (false returns null)
     * @param backgroundColor Background color for outline contrast (default Qt::black)
     * @return QImage (Format_ARGB32_Premultiplied) or null if showNumbers=false or no zones
     */
    static QImage build(const QVariantList& zones,
                        const QSize& size,
                        const QColor& numberColor,
                        bool showNumbers,
                        const QColor& backgroundColor = Qt::black);
};

} // namespace PlasmaZones
