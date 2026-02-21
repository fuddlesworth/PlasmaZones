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

/**
 * @brief Detect whether each edge of a zone lies at a screen boundary
 * @param zone The zone to check
 * @param screenGeom The reference screen geometry (for fixed mode pixel checks)
 * @return Array of 4 bools: [left, top, right, bottom] — true if at boundary
 */
struct EdgeBoundaries {
    bool left = false;
    bool top = false;
    bool right = false;
    bool bottom = false;
};

static EdgeBoundaries detectEdgeBoundaries(Zone* zone, const QRectF& screenGeom)
{
    EdgeBoundaries edges;

    if (zone->isFixedGeometry()) {
        // Fixed mode: pixel proximity check (within 5px of screen boundary)
        constexpr qreal pixelTolerance = 5.0;
        QRectF fixedGeo = zone->fixedGeometry();
        edges.left = (fixedGeo.left() < pixelTolerance);
        edges.top = (fixedGeo.top() < pixelTolerance);
        edges.right = (fixedGeo.right() > (screenGeom.width() - pixelTolerance));
        edges.bottom = (fixedGeo.bottom() > (screenGeom.height() - pixelTolerance));
    } else {
        // Relative mode: existing check (near 0 or 1, tolerance 0.01)
        constexpr qreal edgeTolerance = 0.01;
        QRectF relGeom = zone->relativeGeometry();
        edges.left = (relGeom.left() < edgeTolerance);
        edges.top = (relGeom.top() < edgeTolerance);
        edges.right = (relGeom.right() > (1.0 - edgeTolerance));
        edges.bottom = (relGeom.bottom() > (1.0 - edgeTolerance));
    }

    return edges;
}

QRectF getZoneGeometryWithGaps(Zone* zone, QScreen* screen, int innerGap, int outerGap, bool useAvailableGeometry)
{
    return getZoneGeometryWithGaps(zone, screen, innerGap, EdgeGaps::uniform(outerGap), useAvailableGeometry);
}

QRectF getZoneGeometryWithGaps(Zone* zone, QScreen* screen, int innerGap, const EdgeGaps& outerGaps, bool useAvailableGeometry)
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

    // Detect which edges are at screen boundaries
    QRectF screenGeom = useAvailableGeometry ? ScreenManager::actualAvailableGeometry(screen) : screen->geometry();
    EdgeBoundaries edges = detectEdgeBoundaries(zone, screenGeom);

    // Calculate adjustments for each edge
    qreal leftAdj = edges.left ? outerGaps.left : (innerGap / 2.0);
    qreal topAdj = edges.top ? outerGaps.top : (innerGap / 2.0);
    qreal rightAdj = edges.right ? outerGaps.right : (innerGap / 2.0);
    qreal bottomAdj = edges.bottom ? outerGaps.bottom : (innerGap / 2.0);

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

EdgeGaps getEffectiveOuterGaps(Layout* layout, ISettings* settings)
{
    // Check for layout-specific per-side override first
    if (layout && layout->usePerSideOuterGap() && layout->hasPerSideOuterGapOverride()) {
        EdgeGaps gaps = layout->rawOuterGaps();
        // Fill in -1 values from global per-side or uniform
        int fallback = getEffectiveOuterGap(nullptr, settings); // global uniform fallback
        if (settings && settings->usePerSideOuterGap()) {
            if (gaps.top < 0) gaps.top = settings->outerGapTop();
            if (gaps.bottom < 0) gaps.bottom = settings->outerGapBottom();
            if (gaps.left < 0) gaps.left = settings->outerGapLeft();
            if (gaps.right < 0) gaps.right = settings->outerGapRight();
        } else {
            if (gaps.top < 0) gaps.top = fallback;
            if (gaps.bottom < 0) gaps.bottom = fallback;
            if (gaps.left < 0) gaps.left = fallback;
            if (gaps.right < 0) gaps.right = fallback;
        }
        return gaps;
    }

    // Check for layout-specific uniform override
    if (layout && layout->hasOuterGapOverride()) {
        return EdgeGaps::uniform(layout->outerGap());
    }

    // Fall back to global settings
    if (settings) {
        if (settings->usePerSideOuterGap()) {
            return {settings->outerGapTop(), settings->outerGapBottom(),
                    settings->outerGapLeft(), settings->outerGapRight()};
        }
        return EdgeGaps::uniform(settings->outerGap());
    }

    return EdgeGaps::uniform(Defaults::OuterGap);
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

    int zonePadding = getEffectiveZonePadding(layout, settings);
    EdgeGaps outerGaps = getEffectiveOuterGaps(layout, settings);

    QJsonArray arr;
    for (Zone* zone : layout->zones()) {
        if (!isZoneEmpty(zone)) {
            continue;
        }
        QRectF geom = getZoneGeometryWithGaps(zone, screen, zonePadding, outerGaps, useAvail);
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
