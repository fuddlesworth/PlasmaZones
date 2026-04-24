// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorZones/GeometryUtils.h>
#include <PhosphorEngineApi/IGeometrySettings.h>
#include <PhosphorIdentity/VirtualScreenId.h>
#include <PhosphorScreens/Manager.h>
#include <PhosphorScreens/ScreenIdentity.h>
#include <PhosphorZones/Layout.h>
#include <PhosphorZones/Zone.h>
#include <PhosphorZones/ZoneDefaults.h>
#include <PhosphorZones/ZoneJsonKeys.h>

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QScreen>
#include <QVariantMap>

#include <algorithm>
#include <cmath>

using PhosphorEngineApi::IGeometrySettings;
using PhosphorEngineApi::ZoneAssignmentEntry;
namespace GeometryDefaults = PhosphorEngineApi::GeometryDefaults;
namespace PerScreenSnappingKey = PhosphorEngineApi::PerScreenSnappingKey;

namespace PhosphorZones {

static QVariantMap getPerScreenSnappingWithFallback(IGeometrySettings* settings, const QString& screenId)
{
    QVariantMap result = settings->getPerScreenSnappingSettings(screenId);
    if (result.isEmpty() && PhosphorIdentity::VirtualScreenId::isVirtual(screenId)) {
        result = settings->getPerScreenSnappingSettings(PhosphorIdentity::VirtualScreenId::extractPhysicalId(screenId));
    }
    return result;
}

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

QRectF calculateZoneGeometry(PhosphorZones::Zone* zone, QScreen* screen)
{
    if (!zone || !screen) {
        return QRectF();
    }
    return zone->calculateAbsoluteGeometry(QRectF(screen->geometry()));
}

QRectF calculateZoneGeometryInAvailableArea(Phosphor::Screens::ScreenManager* mgr, PhosphorZones::Zone* zone,
                                            QScreen* screen)
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

EdgeBoundaries detectEdgeBoundaries(PhosphorZones::Zone* zone, const QRectF& screenGeom)
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

QRectF applyGapsToZoneGeometry(const QRectF& zoneGeom, PhosphorZones::Zone* zone, const QRectF& referenceGeom,
                               int innerGap, const ::PhosphorLayout::EdgeGaps& outerGaps,
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
    const QRectF screenGeom = screen->geometry();
    return QRectF(geometry.x() - screenGeom.x(), geometry.y() - screenGeom.y(), geometry.width(), geometry.height());
}

QRectF availableAreaToOverlayCoordinates(const QRectF& geometry, const QRect& overlayGeometry)
{
    return QRectF(geometry.x() - overlayGeometry.x(), geometry.y() - overlayGeometry.y(), geometry.width(),
                  geometry.height());
}

QRectF getZoneGeometryWithGaps(Phosphor::Screens::ScreenManager* mgr, PhosphorZones::Zone* zone, QScreen* screen,
                               int innerGap, const ::PhosphorLayout::EdgeGaps& outerGaps, bool useAvailableGeometry,
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

QRectF getZoneGeometryWithGaps(Phosphor::Screens::ScreenManager* mgr, PhosphorZones::Zone* zone,
                               const QRect& screenGeometry, const QRect& availableGeometry, int innerGap,
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

QRectF getZoneGeometryForScreenF(Phosphor::Screens::ScreenManager* mgr, PhosphorZones::Zone* zone, QScreen* screen,
                                 const QString& screenId, PhosphorZones::Layout* layout, IGeometrySettings* settings)
{
    if (!zone) {
        return QRectF();
    }
    int zonePadding = getEffectiveZonePadding(layout, settings, screenId);
    ::PhosphorLayout::EdgeGaps outerGaps = getEffectiveOuterGaps(layout, settings, screenId);
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

QRect getZoneGeometryForScreen(Phosphor::Screens::ScreenManager* mgr, PhosphorZones::Zone* zone, QScreen* screen,
                               const QString& screenId, PhosphorZones::Layout* layout, IGeometrySettings* settings)
{
    QRectF geoF = getZoneGeometryForScreenF(mgr, zone, screen, screenId, layout, settings);
    return geoF.isValid() ? snapToRect(geoF) : QRect();
}

int getEffectiveZonePadding(PhosphorZones::Layout* layout, IGeometrySettings* settings, const QString& screenId)
{
    if (!screenId.isEmpty() && settings) {
        QVariantMap perScreen = getPerScreenSnappingWithFallback(settings, screenId);
        auto it = perScreen.constFind(QLatin1String(PerScreenSnappingKey::ZonePadding));
        if (it != perScreen.constEnd()) {
            return it->toInt();
        }
    }
    if (layout && layout->hasZonePaddingOverride()) {
        return layout->zonePadding();
    }
    if (settings) {
        return settings->zonePadding();
    }
    return GeometryDefaults::ZonePadding;
}

QRect snapToRect(const QRectF& rf)
{
    const int left = qRound(rf.x());
    const int top = qRound(rf.y());
    const int right = qRound(rf.x() + rf.width());
    const int bottom = qRound(rf.y() + rf.height());
    return QRect(left, top, std::max(0, right - left), std::max(0, bottom - top));
}

::PhosphorLayout::EdgeGaps getEffectiveOuterGaps(PhosphorZones::Layout* layout, IGeometrySettings* settings,
                                                 const QString& screenId)
{
    if (!screenId.isEmpty() && settings) {
        QVariantMap perScreen = getPerScreenSnappingWithFallback(settings, screenId);
        if (!perScreen.isEmpty()) {
            auto usePerSideIt = perScreen.constFind(QLatin1String(PerScreenSnappingKey::UsePerSideOuterGap));
            bool usePerSide = (usePerSideIt != perScreen.constEnd()) ? usePerSideIt->toBool() : false;
            if (usePerSide) {
                auto topIt = perScreen.constFind(QLatin1String(PerScreenSnappingKey::OuterGapTop));
                auto bottomIt = perScreen.constFind(QLatin1String(PerScreenSnappingKey::OuterGapBottom));
                auto leftIt = perScreen.constFind(QLatin1String(PerScreenSnappingKey::OuterGapLeft));
                auto rightIt = perScreen.constFind(QLatin1String(PerScreenSnappingKey::OuterGapRight));
                if (topIt != perScreen.constEnd() || bottomIt != perScreen.constEnd() || leftIt != perScreen.constEnd()
                    || rightIt != perScreen.constEnd()) {
                    auto uniformIt = perScreen.constFind(QLatin1String(PerScreenSnappingKey::OuterGap));
                    int fallback = (uniformIt != perScreen.constEnd()) ? uniformIt->toInt() : settings->outerGap();
                    return {(topIt != perScreen.constEnd()) ? topIt->toInt() : fallback,
                            (bottomIt != perScreen.constEnd()) ? bottomIt->toInt() : fallback,
                            (leftIt != perScreen.constEnd()) ? leftIt->toInt() : fallback,
                            (rightIt != perScreen.constEnd()) ? rightIt->toInt() : fallback};
                }
            }
            auto uniformIt = perScreen.constFind(QLatin1String(PerScreenSnappingKey::OuterGap));
            if (uniformIt != perScreen.constEnd()) {
                return ::PhosphorLayout::EdgeGaps::uniform(uniformIt->toInt());
            }
        }
    }
    if (layout && layout->usePerSideOuterGap() && layout->hasPerSideOuterGapOverride()) {
        ::PhosphorLayout::EdgeGaps gaps = layout->rawOuterGaps();
        if (settings && settings->usePerSideOuterGap()) {
            if (gaps.top < 0)
                gaps.top = settings->outerGapTop();
            if (gaps.bottom < 0)
                gaps.bottom = settings->outerGapBottom();
            if (gaps.left < 0)
                gaps.left = settings->outerGapLeft();
            if (gaps.right < 0)
                gaps.right = settings->outerGapRight();
        } else {
            int fallback = settings ? settings->outerGap() : GeometryDefaults::OuterGap;
            if (gaps.top < 0)
                gaps.top = fallback;
            if (gaps.bottom < 0)
                gaps.bottom = fallback;
            if (gaps.left < 0)
                gaps.left = fallback;
            if (gaps.right < 0)
                gaps.right = fallback;
        }
        return gaps;
    }
    if (layout && layout->hasOuterGapOverride()) {
        return ::PhosphorLayout::EdgeGaps::uniform(layout->outerGap());
    }
    if (settings) {
        if (settings->usePerSideOuterGap()) {
            return {settings->outerGapTop(), settings->outerGapBottom(), settings->outerGapLeft(),
                    settings->outerGapRight()};
        }
        return ::PhosphorLayout::EdgeGaps::uniform(settings->outerGap());
    }
    return ::PhosphorLayout::EdgeGaps::uniform(GeometryDefaults::OuterGap);
}

QRectF effectiveScreenGeometry(Phosphor::Screens::ScreenManager* mgr, PhosphorZones::Layout* layout, QScreen* screen)
{
    if (!screen) {
        return QRectF();
    }
    if (layout && layout->useFullScreenGeometry()) {
        return screen->geometry();
    }
    return mgr ? mgr->actualAvailableGeometry(screen) : screen->availableGeometry();
}

QRectF effectiveScreenGeometry(Phosphor::Screens::ScreenManager* mgr, PhosphorZones::Layout* layout,
                               const QString& screenId)
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
    return QRectF(zone.value(::PhosphorZones::ZoneJsonKeys::X).toDouble(),
                  zone.value(::PhosphorZones::ZoneJsonKeys::Y).toDouble(),
                  zone.value(::PhosphorZones::ZoneJsonKeys::Width).toDouble(),
                  zone.value(::PhosphorZones::ZoneJsonKeys::Height).toDouble());
}

void setZoneGeometry(QVariantMap& zone, const QRectF& rect)
{
    zone[::PhosphorZones::ZoneJsonKeys::X] = rect.x();
    zone[::PhosphorZones::ZoneJsonKeys::Y] = rect.y();
    zone[::PhosphorZones::ZoneJsonKeys::Width] = rect.width();
    zone[::PhosphorZones::ZoneJsonKeys::Height] = rect.height();
}

void enforceWindowMinSizes(QVector<QRect>& zones, const QVector<QSize>& minSizes, int gapThreshold, int innerGap)
{
    if (zones.size() != minSizes.size()) {
        return;
    }
    for (int i = 0; i < zones.size(); ++i) {
        QRect& zone = zones[i];
        const QSize& minSize = minSizes[i];
        if (minSize.isEmpty()) {
            continue;
        }
        int needWidth = minSize.width() - zone.width();
        int needHeight = minSize.height() - zone.height();
        if (needWidth <= 0 && needHeight <= 0) {
            continue;
        }
        for (int j = 0; j < zones.size(); ++j) {
            if (i == j)
                continue;
            QRect& neighbor = zones[j];
            bool adjacentH = (std::abs(zone.right() - neighbor.left()) <= gapThreshold
                              || std::abs(neighbor.right() - zone.left()) <= gapThreshold)
                && zone.top() < neighbor.bottom() && zone.bottom() > neighbor.top();
            bool adjacentV = (std::abs(zone.bottom() - neighbor.top()) <= gapThreshold
                              || std::abs(neighbor.bottom() - zone.top()) <= gapThreshold)
                && zone.left() < neighbor.right() && zone.right() > neighbor.left();

            if (needWidth > 0 && adjacentH) {
                int steal = std::min(needWidth, neighbor.width() / 3);
                if (steal <= 0)
                    continue;
                if (zone.right() <= neighbor.left() + gapThreshold) {
                    zone.setRight(zone.right() + steal);
                    neighbor.setLeft(neighbor.left() + steal);
                } else {
                    zone.setLeft(zone.left() - steal);
                    neighbor.setRight(neighbor.right() - steal);
                }
                needWidth -= steal;
            }
            if (needHeight > 0 && adjacentV) {
                int steal = std::min(needHeight, neighbor.height() / 3);
                if (steal <= 0)
                    continue;
                if (zone.bottom() <= neighbor.top() + gapThreshold) {
                    zone.setBottom(zone.bottom() + steal);
                    neighbor.setTop(neighbor.top() + steal);
                } else {
                    zone.setTop(zone.top() - steal);
                    neighbor.setBottom(neighbor.bottom() - steal);
                }
                needHeight -= steal;
            }
        }
    }
    removeZoneOverlaps(zones, minSizes, innerGap);
}

void removeZoneOverlaps(QVector<QRect>& zones, const QVector<QSize>& minSizes, int innerGap)
{
    for (int i = 0; i < zones.size(); ++i) {
        for (int j = i + 1; j < zones.size(); ++j) {
            QRect& a = zones[i];
            QRect& b = zones[j];
            QRect overlap = a.intersected(b);
            if (overlap.isEmpty()) {
                continue;
            }
            int surplusA_w = a.width() - (minSizes.size() > i ? minSizes[i].width() : 0);
            int surplusB_w = b.width() - (minSizes.size() > j ? minSizes[j].width() : 0);
            int surplusA_h = a.height() - (minSizes.size() > i ? minSizes[i].height() : 0);
            int surplusB_h = b.height() - (minSizes.size() > j ? minSizes[j].height() : 0);

            if (overlap.width() < overlap.height()) {
                int mid = overlap.left() + overlap.width() / 2;
                if (surplusA_w >= surplusB_w) {
                    a.setRight(mid - innerGap / 2);
                    b.setLeft(mid + (innerGap + 1) / 2);
                } else {
                    b.setRight(mid - innerGap / 2);
                    a.setLeft(mid + (innerGap + 1) / 2);
                }
            } else {
                int mid = overlap.top() + overlap.height() / 2;
                if (surplusA_h >= surplusB_h) {
                    a.setBottom(mid - innerGap / 2);
                    b.setTop(mid + (innerGap + 1) / 2);
                } else {
                    b.setBottom(mid - innerGap / 2);
                    a.setTop(mid + (innerGap + 1) / 2);
                }
            }
        }
    }
}

QString rectToJson(const QRect& rect)
{
    QJsonObject obj;
    obj[::PhosphorZones::ZoneJsonKeys::X] = rect.x();
    obj[::PhosphorZones::ZoneJsonKeys::Y] = rect.y();
    obj[::PhosphorZones::ZoneJsonKeys::Width] = rect.width();
    obj[::PhosphorZones::ZoneJsonKeys::Height] = rect.height();
    return QString::fromUtf8(QJsonDocument(obj).toJson(QJsonDocument::Compact));
}

QString serializeZoneAssignments(const QVector<ZoneAssignmentEntry>& entries)
{
    if (entries.isEmpty()) {
        return QStringLiteral("[]");
    }
    QJsonArray array;
    for (const ZoneAssignmentEntry& entry : entries) {
        QJsonObject obj;
        obj[QLatin1String("windowId")] = entry.windowId;
        obj[QLatin1String("sourceZoneId")] = entry.sourceZoneId;
        obj[QLatin1String("targetZoneId")] = entry.targetZoneId;
        if (!entry.targetZoneIds.isEmpty()) {
            QJsonArray zoneIdsArr;
            for (const QString& zid : entry.targetZoneIds)
                zoneIdsArr.append(zid);
            obj[QLatin1String("targetZoneIds")] = zoneIdsArr;
        }
        obj[::PhosphorZones::ZoneJsonKeys::X] = entry.targetGeometry.x();
        obj[::PhosphorZones::ZoneJsonKeys::Y] = entry.targetGeometry.y();
        obj[::PhosphorZones::ZoneJsonKeys::Width] = entry.targetGeometry.width();
        obj[::PhosphorZones::ZoneJsonKeys::Height] = entry.targetGeometry.height();
        array.append(obj);
    }
    return QString::fromUtf8(QJsonDocument(array).toJson(QJsonDocument::Compact));
}

} // namespace GeometryUtils
} // namespace PhosphorZones
