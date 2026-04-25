// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "layoutloader.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>

namespace PlasmaZones::ShaderRender {
namespace {

// Default per-zone fill cycle, used when the layout JSON doesn't
// override.  Mirrors the Phosphor brand palette the site galleries
// use, so the preview reads as "this matches the rest of the docs."
const std::array<QColor, 6> kFillCycle = {{
    QColor::fromRgbF(0.13f, 0.83f, 0.93f, 0.35f),  // cyan-400
    QColor::fromRgbF(0.66f, 0.33f, 0.97f, 0.35f),  // brand-purple
    QColor::fromRgbF(0.20f, 0.83f, 0.60f, 0.35f),  // emerald-400
    QColor::fromRgbF(0.98f, 0.44f, 0.52f, 0.35f),  // rose-400
    QColor::fromRgbF(0.38f, 0.65f, 0.98f, 0.35f),  // link
    QColor::fromRgbF(0.75f, 0.52f, 0.99f, 0.35f),  // purple-400
}};

QColor parseHexColor(const QString& hex, const QColor& fallback)
{
    if (hex.isEmpty()) return fallback;
    QColor c(hex);
    return c.isValid() ? c : fallback;
}

} // namespace

bool loadLayoutZones(const QString& layoutPath,
                     const QSize& resolution,
                     QVector<Zone>& outZones)
{
    QFile f(layoutPath);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return false;
    QJsonParseError err{};
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &err);
    f.close();
    if (err.error != QJsonParseError::NoError || !doc.isObject()) return false;

    const QJsonObject obj = doc.object();
    const QJsonArray zoneArr = obj.value(QLatin1String("zones")).toArray();

    const qreal w = resolution.width();
    const qreal h = resolution.height();
    int idx = 0;

    for (const auto& v : zoneArr) {
        if (!v.isObject()) continue;
        const QJsonObject z = v.toObject();
        const QJsonObject geom = z.value(QLatin1String("relativeGeometry")).toObject();

        Zone zone;
        zone.rect = QRectF(
            geom.value(QLatin1String("x")).toDouble() * w,
            geom.value(QLatin1String("y")).toDouble() * h,
            geom.value(QLatin1String("width")).toDouble() * w,
            geom.value(QLatin1String("height")).toDouble() * h);

        zone.zoneNumber = z.value(QLatin1String("zoneNumber")).toInt(idx + 1);

        // Per-zone color overrides if the layout supplies them, else
        // the brand-cycle fallback so adjacent zones remain distinct.
        zone.fillColor   = parseHexColor(
            z.value(QLatin1String("highlightColor")).toString(),
            kFillCycle[idx % kFillCycle.size()]);
        zone.borderColor = parseHexColor(
            z.value(QLatin1String("borderColor")).toString(),
            QColor::fromRgbF(0.9f, 0.93f, 1.0f, 0.55f));
        zone.borderWidth  = z.value(QLatin1String("borderWidth")).toDouble(1.5);
        zone.borderRadius = z.value(QLatin1String("borderRadius")).toDouble(8.0);
        // Highlight every zone for previews — without at least one
        // active zone, audio-reactive and "vitality"-driven shaders
        // render in their dormant state, which isn't representative.
        zone.isHighlighted = true;

        outZones.append(zone);
        ++idx;
    }

    return !outZones.isEmpty();
}

} // namespace PlasmaZones::ShaderRender
