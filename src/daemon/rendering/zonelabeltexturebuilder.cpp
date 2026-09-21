// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "zonelabeltexturebuilder.h"

#include <QPainter>
#include <QPainterPath>
#include <QFont>
#include <QFontMetricsF>
#include <QImage>

#include <utility>

#include "core/types/constants.h"

using PhosphorRendering::ZoneLabelTexture;
using PhosphorRendering::ZoneLabelTile;

namespace PlasmaZones {

namespace {

constexpr int kGridUnit = 8; // Match Kirigami.Units.gridUnit

QColor outlineColorFor(const QColor& textColor, const QColor& backgroundColor)
{
    const qreal luminance = textColor.redF() * 0.299 + textColor.greenF() * 0.587 + textColor.blueF() * 0.114;
    if (luminance > 0.5) {
        return QColor::fromRgbF(backgroundColor.redF() * 0.2, backgroundColor.greenF() * 0.2,
                                backgroundColor.blueF() * 0.2, 0.8);
    }
    return QColor::fromRgbF(1.0 - backgroundColor.redF() * 0.2, 1.0 - backgroundColor.greenF() * 0.2,
                            1.0 - backgroundColor.blueF() * 0.2, 0.8);
}

} // namespace

ZoneLabelTexture ZoneLabelTextureBuilder::build(const QVariantList& zones, const QSize& size, qreal devicePixelRatio,
                                                const QColor& labelFontColor, bool showNumbers,
                                                const QColor& backgroundColor, const QString& fontFamily,
                                                qreal fontSizeScale, int fontWeight, bool fontItalic,
                                                bool fontUnderline, bool fontStrikeout)
{
    ZoneLabelTexture result;
    if (!showNumbers || zones.isEmpty() || size.width() <= 0 || size.height() <= 0) {
        return result; // empty payload
    }
    // Everything below is authored in LOGICAL units — the zone rects arrive in
    // them, and the font size, tile margin and outline width are all tuned in
    // them. Only the raster resolution changes: the payload's size and tile
    // offsets are device pixels, and each tile's QPainter carries the scale, so
    // the glyphs are drawn at the resolution the shader samples them at rather
    // than being upscaled into it. See the header for why that matters beyond
    // the glyphs themselves.
    const qreal dpr = devicePixelRatio > 0.0 ? devicePixelRatio : 1.0;
    result.size = QSize(qRound(size.width() * dpr), qRound(size.height() * dpr));
    if (result.size.isEmpty()) {
        return ZoneLabelTexture{};
    }

    const QColor outlineColor = outlineColorFor(labelFontColor, backgroundColor);
    const QColor fillColor = labelFontColor;
    const QRect deviceRect(QPoint(0, 0), result.size);

    // Slack around glyph content so the 2px outline stroke + antialiasing aren't
    // clipped at a tile's edges.
    constexpr int kTileMargin = 3;

    for (const QVariant& zoneVar : zones) {
        const QVariantMap z = zoneVar.toMap();
        const qreal x = z.value(QLatin1String(::PhosphorZones::ZoneJsonKeys::X), 0).toDouble();
        const qreal y = z.value(QLatin1String(::PhosphorZones::ZoneJsonKeys::Y), 0).toDouble();
        const qreal w = z.value(QLatin1String(::PhosphorZones::ZoneJsonKeys::Width), 0).toDouble();
        const qreal h = z.value(QLatin1String(::PhosphorZones::ZoneJsonKeys::Height), 0).toDouble();
        const int zoneNumber = z.value(QLatin1String(::PhosphorZones::ZoneJsonKeys::ZoneNumber), 0).toInt();

        if (w <= 0 || h <= 0) {
            continue;
        }

        const QString text = QString::number(zoneNumber);
        const qreal fontPixelSize = qMax(static_cast<qreal>(kGridUnit), qMin(w, h) * 0.25) * fontSizeScale;

        QFont font;
        if (!fontFamily.isEmpty()) {
            font.setFamily(fontFamily);
        }
        font.setPixelSize(static_cast<int>(fontPixelSize));
        font.setWeight(static_cast<QFont::Weight>(qBound(100, fontWeight, 900)));
        font.setItalic(fontItalic);

        const QRectF rect(x, y, w, h);
        const QPointF center = rect.center();

        QPainterPath path;
        path.addText(0, 0, font, text);

        // Center the text in the zone rect (addText uses baseline at origin).
        // After translate, `path` is in screen coordinates.
        const QRectF textBounds = path.boundingRect();
        const qreal translateX = center.x() - textBounds.center().x();
        const qreal translateY = center.y() - textBounds.center().y();
        path.translate(translateX, translateY);

        // QPainterPath::addText only includes glyph outlines, not text
        // decorations. Compute underline/strikeout rects (screen coords) once,
        // so they contribute to the tile bounds AND get painted below.
        QList<QRectF> decoRects;
        if (fontUnderline || fontStrikeout) {
            const QFontMetricsF fm(font);
            const qreal lineThickness = qMax(1.0, fm.lineWidth());
            const qreal baselineY = translateY; // baseline was at y=0 before translation
            const qreal lineLeft = textBounds.left() + translateX;
            const qreal lineWidth = textBounds.width();
            const auto decoRect = [&](qreal yOffset) {
                return QRectF(lineLeft, baselineY + yOffset - lineThickness / 2.0, lineWidth, lineThickness);
            };
            if (fontUnderline) {
                decoRects.append(decoRect(fm.underlinePos()));
            }
            if (fontStrikeout) {
                decoRects.append(decoRect(-fm.strikeOutPos()));
            }
        }

        // Tile = glyph + decoration bounds, inflated for the outline stroke/AA
        // in logical units, then taken to the device grid and clamped to the
        // texture. Only this small region is allocated — the rest of the
        // screen-addressed texture stays transparent. Aligned OUTWARD after
        // scaling rather than scaling an already-aligned logical rect: at a
        // fractional ratio the latter can crop a device column of the stroke
        // the margin was there to protect.
        QRectF contentF = path.boundingRect();
        for (const QRectF& d : decoRects) {
            contentF = contentF.united(d);
        }
        contentF = contentF.adjusted(-kTileMargin, -kTileMargin, kTileMargin, kTileMargin);
        const QRect tileRect =
            QRectF(contentF.x() * dpr, contentF.y() * dpr, contentF.width() * dpr, contentF.height() * dpr)
                .toAlignedRect()
                .intersected(deviceRect);
        if (tileRect.isEmpty()) {
            continue;
        }

        QImage tile(tileRect.size(), QImage::Format_ARGB32_Premultiplied);
        if (tile.isNull()) {
            continue; // allocation failure: skip this label rather than abort the payload
        }
        // Deliberately NOT setDevicePixelRatio: the scale is applied on the
        // painter below, and a ratio on the image would apply it a second time
        // — to the painter here, and again to every drawImage that composites
        // these tiles (toImage(), the RHI upload's staging path), which treat
        // a tagged image as logical-sized.
        tile.fill(Qt::transparent);

        QPainter painter(&tile);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setRenderHint(QPainter::TextAntialiasing);
        painter.setRenderHint(QPainter::SmoothPixmapTransform);
        // Map LOGICAL screen coordinates into this tile's local DEVICE space.
        // Order is load-bearing: translate-then-scale composes as
        // p_device = dpr * p_logical - tileRect.topLeft(), which is the tile's
        // own corner measured on the device grid. The reverse order would
        // scale the offset too and put every glyph at dpr times its distance
        // from the origin.
        painter.translate(-tileRect.topLeft());
        painter.scale(dpr, dpr);

        const QPen outlinePen(outlineColor, 2.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
        // Draw outline (stroke) then fill — identical order to the prior
        // single-image builder, so glyph appearance is byte-for-byte the same.
        painter.setPen(outlinePen);
        painter.setBrush(Qt::NoBrush);
        painter.drawPath(path);
        painter.setPen(Qt::NoPen);
        painter.setBrush(fillColor);
        painter.drawPath(path);

        for (const QRectF& d : decoRects) {
            QPainterPath lp;
            lp.addRect(d);
            painter.setPen(outlinePen);
            painter.setBrush(Qt::NoBrush);
            painter.drawPath(lp);
            painter.setPen(Qt::NoPen);
            painter.setBrush(fillColor);
            painter.drawPath(lp);
        }
        painter.end();

        result.tiles.append(ZoneLabelTile{std::move(tile), tileRect.topLeft()});
    }

    // All zones degenerate / clipped away ⇒ nothing to show.
    if (result.tiles.isEmpty()) {
        return ZoneLabelTexture{};
    }
    return result;
}

} // namespace PlasmaZones
