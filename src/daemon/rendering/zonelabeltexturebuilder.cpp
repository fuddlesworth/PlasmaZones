// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "zonelabeltexturebuilder.h"

#include <QPainter>
#include <QPainterPath>
#include <QFont>
#include <QFontMetricsF>

#include "../../core/constants.h"

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

QImage ZoneLabelTextureBuilder::build(const QVariantList& zones,
                                      const QSize& size,
                                      const QColor& labelFontColor,
                                      bool showNumbers,
                                      const QColor& backgroundColor,
                                      const QString& fontFamily,
                                      qreal fontSizeScale,
                                      int fontWeight,
                                      bool fontItalic,
                                      bool fontUnderline,
                                      bool fontStrikeout)
{
    if (!showNumbers || zones.isEmpty() || size.width() <= 0 || size.height() <= 0) {
        return QImage();
    }

    QImage image(size, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);

    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setRenderHint(QPainter::TextAntialiasing);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);

    const QColor outlineColor = outlineColorFor(labelFontColor, backgroundColor);
    const QColor fillColor = labelFontColor;

    for (const QVariant& zoneVar : zones) {
        const QVariantMap z = zoneVar.toMap();
        const qreal x = z.value(QLatin1String(JsonKeys::X), 0).toDouble();
        const qreal y = z.value(QLatin1String(JsonKeys::Y), 0).toDouble();
        const qreal w = z.value(QLatin1String(JsonKeys::Width), 0).toDouble();
        const qreal h = z.value(QLatin1String(JsonKeys::Height), 0).toDouble();
        const int zoneNumber = z.value(QLatin1String(JsonKeys::ZoneNumber), 0).toInt();

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

        // Center the text in the zone rect (addText uses baseline at origin)
        const QRectF textBounds = path.boundingRect();
        const qreal translateX = center.x() - textBounds.center().x();
        const qreal translateY = center.y() - textBounds.center().y();
        path.translate(translateX, translateY);

        // Draw outline (stroke) then fill
        const QPen outlinePen(outlineColor, 2.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
        painter.setPen(outlinePen);
        painter.setBrush(Qt::NoBrush);
        painter.drawPath(path);
        painter.setPen(Qt::NoPen);
        painter.setBrush(fillColor);
        painter.drawPath(path);

        // QPainterPath::addText only includes glyph outlines, not text decorations.
        // Draw underline/strikeout manually using font metrics.
        if (fontUnderline || fontStrikeout) {
            const QFontMetricsF fm(font);
            const qreal lineThickness = qMax(1.0, fm.lineWidth());
            // Baseline was at y=0 before translation
            const qreal baselineY = translateY;
            const qreal lineLeft = textBounds.left() + translateX;
            const qreal lineWidth = textBounds.width();

            auto drawDecoration = [&](qreal yOffset) {
                const QRectF lineRect(lineLeft, baselineY + yOffset - lineThickness / 2.0,
                                      lineWidth, lineThickness);
                QPainterPath lp;
                lp.addRect(lineRect);
                painter.setPen(outlinePen);
                painter.setBrush(Qt::NoBrush);
                painter.drawPath(lp);
                painter.setPen(Qt::NoPen);
                painter.setBrush(fillColor);
                painter.drawPath(lp);
            };

            if (fontUnderline) {
                drawDecoration(fm.underlinePos());
            }
            if (fontStrikeout) {
                drawDecoration(-fm.strikeOutPos());
            }
        }
    }

    painter.end();

    return image;
}

} // namespace PlasmaZones
