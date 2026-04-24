// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorZones/GeometryUtils.h>
#include <PhosphorIdentity/VirtualScreenId.h>
#include <PhosphorScreens/Manager.h>
#include <PhosphorScreens/ScreenIdentity.h>
#include <PhosphorZones/Layout.h>
#include <PhosphorZones/Zone.h>
#include <PhosphorZones/ZoneDefaults.h>
#include <PhosphorZones/ZoneJsonKeys.h>

#include <QScreen>
#include <QVariantMap>

namespace PhosphorZones {
namespace GeometryUtils {

namespace {
struct ScreenGeometries
{
    QRect geometry;
    QRect availableGeometry;
};

ScreenGeometries resolveScreenGeometries(Phosphor::Screens::ScreenManager* mgr, const QString& screenId)
{
    if (!mgr) {
        return {};
    }
    return {mgr->screenGeometry(screenId), mgr->screenAvailableGeometry(screenId)};
}

QRectF calculateZoneGeometry(Zone* zone, QScreen* screen)
{
    if (!zone || !screen) {
        return QRectF();
    }
    return zone->calculateAbsoluteGeometry(QRectF(screen->geometry()));
}

QRectF calculateZoneGeometryInAvailableArea(Phosphor::Screens::ScreenManager* mgr, Zone* zone, QScreen* screen)
{
    if (!zone || !screen) {
        return QRectF();
    }
    const QRectF availableGeom = mgr ? mgr->actualAvailableGeometry(screen) : screen->availableGeometry();
    return zone->calculateAbsoluteGeometry(availableGeom);
}

struct EdgeBoundaries
{
    bool left = false;
    bool top = false;
    bool right = false;
    bool bottom = false;
};

EdgeBoundaries detectEdgeBoundaries(Zone* zone, const QRectF& screenGeom)
{
    EdgeBoundaries edges;
    if (zone->isFixedGeometry()) {
        constexpr qreal pixelTolerance = 5.0;
        QRectF fixedGeo = zone->fixedGeometry();
        edges.left = (fixedGeo.left() < pixelTolerance);
        edges.top = (fixedGeo.top() < pixelTolerance);
        edges.right = (fixedGeo.right() > (screenGeom.width() - pixelTolerance));
        edges.bottom = (fixedGeo.bottom() > (screenGeom.height() - pixelTolerance));
    } else {
        constexpr qreal edgeTolerance = 0.01;
        QRectF relGeom = zone->relativeGeometry();
        edges.left = (relGeom.left() < edgeTolerance);
        edges.top = (relGeom.top() < edgeTolerance);
        edges.right = (relGeom.right() > (1.0 - edgeTolerance));
        edges.bottom = (relGeom.bottom() > (1.0 - edgeTolerance));
    }
    return edges;
}

QRectF applyGapsToZoneGeometry(const QRectF& zoneGeom, Zone* zone, const QRectF& referenceGeom, int innerGap,
                               const ::PhosphorLayout::EdgeGaps& outerGaps,
                               const Phosphor::Screens::VirtualScreenDef::PhysicalEdges& physEdges = {true, true, true,
                                                                                                      true})
{
    EdgeBoundaries edges = detectEdgeBoundaries(zone, referenceGeom);
    qreal leftAdj = (edges.left && physEdges.left) ? outerGaps.left : (innerGap / 2.0);
    qreal topAdj = (edges.top && physEdges.top) ? outerGaps.top : (innerGap / 2.0);
    qreal rightAdj = (edges.right && physEdges.right) ? outerGaps.right : (innerGap / 2.0);
    qreal bottomAdj = (edges.bottom && physEdges.bottom) ? outerGaps.bottom : (innerGap / 2.0);
    QRectF geoF = zoneGeom.adjusted(leftAdj, topAdj, -rightAdj, -bottomAdj);
    if (geoF.width() < 1.0) {
        geoF.setWidth(1.0);
    }
    if (geoF.height() < 1.0) {
        geoF.setHeight(1.0);
    }
    return geoF;
}
} // anonymous namespace

QRectF availableAreaToOverlayCoordinates(const QRectF& geometry, QScreen* screen)
{
    if (!screen) {
        return geometry;
    }
    return PhosphorGeometry::availableAreaToOverlayCoordinates(geometry, screen->geometry());
}

QRectF getZoneGeometryWithGaps(Phosphor::Screens::ScreenManager* mgr, Zone* zone, QScreen* screen, int innerGap,
                               const ::PhosphorLayout::EdgeGaps& outerGaps, bool useAvailableGeometry,
                               const QString& screenId)
{
    if (!zone || !screen) {
        return QRectF();
    }
    QRectF geom;
    if (useAvailableGeometry) {
        geom = calculateZoneGeometryInAvailableArea(mgr, zone, screen);
    } else {
        geom = calculateZoneGeometry(zone, screen);
    }
    QRectF screenGeom = useAvailableGeometry
        ? (mgr ? mgr->actualAvailableGeometry(screen) : screen->availableGeometry())
        : screen->geometry();
    Phosphor::Screens::VirtualScreenDef::PhysicalEdges physEdges{true, true, true, true};
    if (mgr) {
        QString resolvedId = screenId.isEmpty() ? Phosphor::Screens::ScreenIdentity::identifierFor(screen) : screenId;
        physEdges = mgr->physicalEdgesFor(resolvedId);
    }
    return applyGapsToZoneGeometry(geom, zone, screenGeom, innerGap, outerGaps, physEdges);
}

QRectF getZoneGeometryWithGaps(Phosphor::Screens::ScreenManager* mgr, Zone* zone, const QRect& screenGeometry,
                               const QRect& availableGeometry, int innerGap,
                               const ::PhosphorLayout::EdgeGaps& outerGaps, bool useAvailableGeometry,
                               const QString& screenId)
{
    if (!zone) {
        return QRectF();
    }
    QRectF referenceGeom = useAvailableGeometry ? QRectF(availableGeometry) : QRectF(screenGeometry);
    QRectF geom = zone->calculateAbsoluteGeometry(referenceGeom);
    if (!screenId.isEmpty() && mgr) {
        auto pe = mgr->physicalEdgesFor(screenId);
        return applyGapsToZoneGeometry(geom, zone, referenceGeom, innerGap, outerGaps, pe);
    }
    return applyGapsToZoneGeometry(geom, zone, referenceGeom, innerGap, outerGaps);
}

QRectF getZoneGeometryForScreenF(Phosphor::Screens::ScreenManager* mgr, Zone* zone, QScreen* screen,
                                 const QString& screenId, Layout* layout, int zonePadding,
                                 const ::PhosphorLayout::EdgeGaps& outerGaps)
{
    if (!zone) {
        return QRectF();
    }
    bool useAvail = !(layout && layout->useFullScreenGeometry());
    auto [vsGeom, vsAvailGeom] = resolveScreenGeometries(mgr, screenId);
    if (vsGeom.isValid()) {
        QRect availGeom = vsAvailGeom.isValid() ? vsAvailGeom : vsGeom;
        return getZoneGeometryWithGaps(mgr, zone, vsGeom, availGeom, zonePadding, outerGaps, useAvail, screenId);
    }
    if (PhosphorIdentity::VirtualScreenId::isVirtual(screenId)) {
        return QRectF();
    }
    if (screen) {
        return getZoneGeometryWithGaps(mgr, zone, screen, zonePadding, outerGaps, useAvail, screenId);
    }
    return QRectF();
}

QRect getZoneGeometryForScreen(Phosphor::Screens::ScreenManager* mgr, Zone* zone, QScreen* screen,
                               const QString& screenId, Layout* layout, int zonePadding,
                               const ::PhosphorLayout::EdgeGaps& outerGaps)
{
    QRectF geoF = getZoneGeometryForScreenF(mgr, zone, screen, screenId, layout, zonePadding, outerGaps);
    return geoF.isValid() ? PhosphorGeometry::snapToRect(geoF) : QRect();
}

QRectF effectiveScreenGeometry(Phosphor::Screens::ScreenManager* mgr, Layout* layout, QScreen* screen)
{
    if (!screen) {
        return QRectF();
    }
    if (layout && layout->useFullScreenGeometry()) {
        return screen->geometry();
    }
    return mgr ? mgr->actualAvailableGeometry(screen) : screen->availableGeometry();
}

QRectF effectiveScreenGeometry(Phosphor::Screens::ScreenManager* mgr, Layout* layout, const QString& screenId)
{
    auto [geom, availGeom] = resolveScreenGeometries(mgr, screenId);
    if (geom.isValid()) {
        if (layout && layout->useFullScreenGeometry()) {
            return QRectF(geom);
        }
        return availGeom.isValid() ? QRectF(availGeom) : QRectF(geom);
    }
    QScreen* screen = Phosphor::Screens::ScreenIdentity::findByIdOrName(screenId);
    return effectiveScreenGeometry(mgr, layout, screen);
}

QRectF extractZoneGeometry(const QVariantMap& zone)
{
    return QRectF(zone.value(ZoneJsonKeys::X).toDouble(), zone.value(ZoneJsonKeys::Y).toDouble(),
                  zone.value(ZoneJsonKeys::Width).toDouble(), zone.value(ZoneJsonKeys::Height).toDouble());
}

void setZoneGeometry(QVariantMap& zone, const QRectF& rect)
{
    zone[ZoneJsonKeys::X] = rect.x();
    zone[ZoneJsonKeys::Y] = rect.y();
    zone[ZoneJsonKeys::Width] = rect.width();
    zone[ZoneJsonKeys::Height] = rect.height();
}

} // namespace GeometryUtils
} // namespace PhosphorZones
