// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "geometryutils.h"
#include "zone.h"
#include "layout.h"
#include "interfaces.h"
#include "constants.h"
#include "screenmanager.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QScreen>
#include <QVariantMap>

namespace PlasmaZones {

namespace GeometryUtils {

static QRectF calculateZoneGeometry(Zone* zone, QScreen* screen)
{
    if (!zone || !screen) {
        return QRectF();
    }

    const QRectF screenGeom = screen->geometry();
    return zone->calculateAbsoluteGeometry(screenGeom);
}

QRectF availableAreaToOverlayCoordinates(const QRectF& geometry, QScreen* screen)
{
    if (!screen) {
        return geometry;
    }

    // The overlay window covers the full screen (geometry()).
    // The geometry parameter is already in absolute screen coordinates
    // (from available or full-screen geometry), so we just need to convert
    // to overlay-local coordinates by subtracting the full screen origin.
    const QRectF screenGeom = screen->geometry();
    return QRectF(geometry.x() - screenGeom.x(), geometry.y() - screenGeom.y(), geometry.width(), geometry.height());
}

static QRectF calculateZoneGeometryInAvailableArea(Zone* zone, QScreen* screen)
{
    if (!zone || !screen) {
        return QRectF();
    }

    // Use actualAvailableGeometry which excludes panels/taskbars (queries PlasmaShell on Wayland)
    const QRectF availableGeom = ScreenManager::actualAvailableGeometry(screen);
    return zone->calculateAbsoluteGeometry(availableGeom);
}

QRectF getZoneGeometryWithGaps(Zone* zone, QScreen* screen, int innerGap, int outerGap, bool useAvailableGeometry)
{
    if (!zone || !screen) {
        return QRectF();
    }

    // Use available geometry (excludes panels/taskbars) or full screen geometry
    QRectF geom;
    if (useAvailableGeometry) {
        geom = calculateZoneGeometryInAvailableArea(zone, screen);
    } else {
        geom = calculateZoneGeometry(zone, screen);
    }

    // Get relative geometry to determine which edges are at screen boundaries
    QRectF relGeom = zone->relativeGeometry();

    // Tolerance for floating point comparison (zones at edge should be within 0.01 of boundary)
    constexpr qreal edgeTolerance = 0.01;

    // Calculate adjustments for each edge
    // Left edge: at boundary (0) -> outerGap, otherwise -> innerGap/2
    qreal leftAdj = (relGeom.left() < edgeTolerance) ? outerGap : (innerGap / 2.0);

    // Top edge: at boundary (0) -> outerGap, otherwise -> innerGap/2
    qreal topAdj = (relGeom.top() < edgeTolerance) ? outerGap : (innerGap / 2.0);

    // Right edge: at boundary (1) -> outerGap, otherwise -> innerGap/2
    qreal rightAdj = (relGeom.right() > (1.0 - edgeTolerance)) ? outerGap : (innerGap / 2.0);

    // Bottom edge: at boundary (1) -> outerGap, otherwise -> innerGap/2
    qreal bottomAdj = (relGeom.bottom() > (1.0 - edgeTolerance)) ? outerGap : (innerGap / 2.0);

    // Apply the adjustments (positive inset from edges)
    geom = geom.adjusted(leftAdj, topAdj, -rightAdj, -bottomAdj);

    return geom;
}

int getEffectiveZonePadding(Layout* layout, ISettings* settings)
{
    // Check for layout-specific override first
    if (layout && layout->hasZonePaddingOverride()) {
        return layout->zonePadding();
    }
    // Fall back to global settings
    if (settings) {
        return settings->zonePadding();
    }
    // Last resort: use default constant
    return Defaults::ZonePadding;
}

int getEffectiveOuterGap(Layout* layout, ISettings* settings)
{
    // Check for layout-specific override first
    if (layout && layout->hasOuterGapOverride()) {
        return layout->outerGap();
    }
    // Fall back to global outerGap setting
    if (settings) {
        return settings->outerGap();
    }
    // Last resort: use default constant
    return Defaults::OuterGap;
}

QRectF effectiveScreenGeometry(Layout* layout, QScreen* screen)
{
    if (!screen) {
        return QRectF();
    }
    if (layout && layout->useFullScreenGeometry()) {
        return screen->geometry();
    }
    return ScreenManager::actualAvailableGeometry(screen);
}

QRectF extractZoneGeometry(const QVariantMap& zone)
{
    return QRectF(zone.value(QLatin1String("x")).toDouble(),
                  zone.value(QLatin1String("y")).toDouble(),
                  zone.value(QLatin1String("width")).toDouble(),
                  zone.value(QLatin1String("height")).toDouble());
}

void setZoneGeometry(QVariantMap& zone, const QRectF& rect)
{
    zone[QLatin1String("x")] = rect.x();
    zone[QLatin1String("y")] = rect.y();
    zone[QLatin1String("width")] = rect.width();
    zone[QLatin1String("height")] = rect.height();
}

QString buildEmptyZonesJson(Layout* layout, QScreen* screen, ISettings* settings,
                            const std::function<bool(const Zone*)>& isZoneEmpty)
{
    if (!layout || !screen) {
        return QStringLiteral("[]");
    }

    bool useAvail = !(layout && layout->useFullScreenGeometry());
    layout->recalculateZoneGeometries(effectiveScreenGeometry(layout, screen));

    QJsonArray arr;
    for (Zone* zone : layout->zones()) {
        if (!isZoneEmpty(zone)) {
            continue;
        }
        int zonePadding = getEffectiveZonePadding(layout, settings);
        int outerGap = getEffectiveOuterGap(layout, settings);
        QRectF geom = getZoneGeometryWithGaps(zone, screen, zonePadding, outerGap, useAvail);
        QRectF overlayGeom = availableAreaToOverlayCoordinates(geom, screen);

        QJsonObject obj;
        obj[JsonKeys::ZoneId] = zone->id().toString();
        obj[JsonKeys::X] = overlayGeom.x();
        obj[JsonKeys::Y] = overlayGeom.y();
        obj[JsonKeys::Width] = overlayGeom.width();
        obj[JsonKeys::Height] = overlayGeom.height();
        obj[JsonKeys::UseCustomColors] = zone->useCustomColors();
        obj[JsonKeys::HighlightColor] = zone->highlightColor().name(QColor::HexArgb);
        obj[JsonKeys::InactiveColor] = zone->inactiveColor().name(QColor::HexArgb);
        obj[JsonKeys::BorderColor] = zone->borderColor().name(QColor::HexArgb);
        obj[JsonKeys::ActiveOpacity] = zone->activeOpacity();
        obj[JsonKeys::InactiveOpacity] = zone->inactiveOpacity();
        obj[JsonKeys::BorderWidth] =
            zone->useCustomColors() ? zone->borderWidth()
                                   : (settings ? settings->borderWidth() : Defaults::BorderWidth);
        obj[JsonKeys::BorderRadius] =
            zone->useCustomColors() ? zone->borderRadius()
                                   : (settings ? settings->borderRadius() : Defaults::BorderRadius);
        arr.append(obj);
    }
    return QString::fromUtf8(QJsonDocument(arr).toJson(QJsonDocument::Compact));
}

} // namespace GeometryUtils

} // namespace PlasmaZones
