// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorRendering/ZoneLabelTexture.h>

#include <QPainter>

namespace PhosphorRendering {

QImage ZoneLabelTexture::toImage() const
{
    if (isEmpty()) {
        return {};
    }
    // SourceOver composite, matching the single-image builder this payload
    // replaced — overlapping glyphs alpha-blend identically.
    QImage image(size, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    QPainter painter(&image);
    for (const ZoneLabelTile& tile : tiles) {
        if (!tile.image.isNull()) {
            painter.drawImage(tile.dest, tile.image);
        }
    }
    painter.end();
    return image;
}

ZoneLabelTexture ZoneLabelTexture::fromImage(const QImage& image)
{
    ZoneLabelTexture t;
    if (!image.isNull() && image.width() > 0 && image.height() > 0) {
        t.size = image.size();
        t.tiles.append(ZoneLabelTile{image, QPoint(0, 0)});
    }
    return t;
}

} // namespace PhosphorRendering
