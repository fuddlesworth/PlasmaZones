// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "geometryutils.h"
#include "zone.h"
#include <geometry_helpers.h>
#include "logging.h"
#include "layout.h"
#include "layoutworker/layoutcomputeservice.h"
#include "interfaces.h"
#include "constants.h"
#include "screenmanager.h"
#include "utils.h"
#include <algorithm>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QScreen>
#include <QVariantMap>

namespace PlasmaZones {

/// Get per-screen snapping settings with virtual screen fallback.
/// Tries the exact screenId first; if empty and the screenId is a virtual
/// screen, falls back to the physical parent's overrides.
static QVariantMap getPerScreenSnappingWithFallback(ISettings* settings, const QString& screenId)
{
    QVariantMap result = settings->getPerScreenSnappingSettings(screenId);
    if (result.isEmpty() && VirtualScreenId::isVirtual(screenId)) {
        result = settings->getPerScreenSnappingSettings(VirtualScreenId::extractPhysicalId(screenId));
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

ScreenGeometries resolveScreenGeometries(const QString& screenId)
{
    auto* mgr = ScreenManager::instance();
    if (!mgr) {
        return {};
    }
    return {mgr->screenGeometry(screenId), mgr->screenAvailableGeometry(screenId)};
}
} // anonymous namespace

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
struct EdgeBoundaries
{
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

/**
 * @brief Apply gap adjustments to a zone's absolute geometry
 * @param zoneGeom Absolute zone geometry (already calculated against reference area)
 * @param zone Zone for edge boundary detection
 * @param referenceGeom Reference geometry for fixed-mode edge detection
 * @param innerGap Gap between adjacent zones (zonePadding)
 * @param outerGaps Per-side edge gaps
 * @return Geometry with gaps applied
 *
 * Shared helper used by both QScreen* and QRect overloads of getZoneGeometryWithGaps.
 */
static QRectF applyGapsToZoneGeometry(const QRectF& zoneGeom, Zone* zone, const QRectF& referenceGeom, int innerGap,
                                      const ::PhosphorLayout::EdgeGaps& outerGaps,
                                      const VirtualScreenDef::PhysicalEdges& physEdges = {true, true, true, true})
{
    // Detect which edges are at screen boundaries
    EdgeBoundaries edges = detectEdgeBoundaries(zone, referenceGeom);

    // Only apply outer gaps on edges that are at the PHYSICAL screen boundary.
    // Internal edges (shared with another virtual screen) get inner gap to avoid
    // double gaps.
    qreal leftAdj = (edges.left && physEdges.left) ? outerGaps.left : (innerGap / 2.0);
    qreal topAdj = (edges.top && physEdges.top) ? outerGaps.top : (innerGap / 2.0);
    qreal rightAdj = (edges.right && physEdges.right) ? outerGaps.right : (innerGap / 2.0);
    qreal bottomAdj = (edges.bottom && physEdges.bottom) ? outerGaps.bottom : (innerGap / 2.0);

    // Apply the adjustments (positive inset from edges)
    QRectF geoF = zoneGeom.adjusted(leftAdj, topAdj, -rightAdj, -bottomAdj);

    // Ensure gaps don't collapse the zone to negative dimensions
    if (geoF.width() < 1.0) {
        geoF.setWidth(1.0);
    }
    if (geoF.height() < 1.0) {
        geoF.setHeight(1.0);
    }

    return geoF;
}

QRectF getZoneGeometryWithGaps(Zone* zone, QScreen* screen, int innerGap, const ::PhosphorLayout::EdgeGaps& outerGaps,
                               bool useAvailableGeometry, const QString& screenId)
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

    // Look up physical edges via ScreenManager so virtual screen internal edges
    // get inner gap instead of outer gap, matching the QRect overload behavior.
    // When the caller provides a virtual screen ID, use it; otherwise fall back
    // to the physical screen identifier (which yields all-true physical edges).
    VirtualScreenDef::PhysicalEdges physEdges{true, true, true, true};
    auto* mgr = ScreenManager::instance();
    if (mgr) {
        QString resolvedId = screenId.isEmpty() ? Utils::screenIdentifier(screen) : screenId;
        physEdges = mgr->physicalEdgesFor(resolvedId);
    }

    return applyGapsToZoneGeometry(geom, zone, screenGeom, innerGap, outerGaps, physEdges);
}

QRectF availableAreaToOverlayCoordinates(const QRectF& geometry, const QRect& overlayGeometry)
{
    return QRectF(geometry.x() - overlayGeometry.x(), geometry.y() - overlayGeometry.y(), geometry.width(),
                  geometry.height());
}

QRectF getZoneGeometryWithGaps(Zone* zone, const QRect& screenGeometry, const QRect& availableGeometry, int innerGap,
                               const ::PhosphorLayout::EdgeGaps& outerGaps, bool useAvailableGeometry,
                               const QString& screenId)
{
    if (!zone) {
        return QRectF();
    }

    // Calculate absolute zone geometry against the chosen reference area
    QRectF referenceGeom = useAvailableGeometry ? QRectF(availableGeometry) : QRectF(screenGeometry);
    QRectF geom = zone->calculateAbsoluteGeometry(referenceGeom);

    // Determine which edges are at the physical screen boundary.
    // For virtual screens, internal edges (shared with another virtual screen) get inner
    // gap instead of outer gap to avoid double gaps at virtual screen boundaries.
    // For physical screens, physicalEdgesFor() returns all-true (all edges are outer).
    // Note: When ScreenManager::instance() is null (e.g. in unit tests), the fallback
    // applies default {true,true,true,true} physEdges. Tests exercising virtual
    // screen gap calculations should provide explicit physEdges via applyGapsToZoneGeometry()
    // directly, rather than relying on this code path.
    if (!screenId.isEmpty()) {
        auto* mgr = ScreenManager::instance();
        if (mgr) {
            auto pe = mgr->physicalEdgesFor(screenId);
            return applyGapsToZoneGeometry(geom, zone, referenceGeom, innerGap, outerGaps, pe);
        }
    }

    return applyGapsToZoneGeometry(geom, zone, referenceGeom, innerGap, outerGaps);
}

QRectF getZoneGeometryForScreenF(Zone* zone, QScreen* screen, const QString& screenId, Layout* layout,
                                 ISettings* settings)
{
    if (!zone) {
        return QRectF();
    }

    int zonePadding = getEffectiveZonePadding(layout, settings, screenId);
    ::PhosphorLayout::EdgeGaps outerGaps = getEffectiveOuterGaps(layout, settings, screenId);
    bool useAvail = !(layout && layout->useFullScreenGeometry());

    auto [vsGeom, vsAvailGeom] = resolveScreenGeometries(screenId);

    if (vsGeom.isValid()) {
        QRect availGeom = vsAvailGeom.isValid() ? vsAvailGeom : vsGeom;
        return getZoneGeometryWithGaps(zone, vsGeom, availGeom, zonePadding, outerGaps, useAvail, screenId);
    }
    // For virtual screen IDs, do NOT fall back to the parent QScreen* — that
    // re-projects relative zone coordinates (defined for the VS sub-region)
    // onto the full physical screen, producing a geometry that lands in a
    // DIFFERENT virtual screen. processBatchEntries then re-derives the
    // window's screen from the geometry center, silently overwriting the
    // stored VS scope and corrupting WindowScreenAssignments. Better to
    // return an invalid rect so the caller skips the entry; the next resnap
    // (after VS state is restored) will succeed.
    if (VirtualScreenId::isVirtual(screenId)) {
        return QRectF();
    }
    if (screen) {
        return getZoneGeometryWithGaps(zone, screen, zonePadding, outerGaps, useAvail, screenId);
    }
    return QRectF();
}

QRect getZoneGeometryForScreen(Zone* zone, QScreen* screen, const QString& screenId, Layout* layout,
                               ISettings* settings)
{
    QRectF geoF = getZoneGeometryForScreenF(zone, screen, screenId, layout, settings);
    return geoF.isValid() ? snapToRect(geoF) : QRect();
}

int getEffectiveZonePadding(Layout* layout, ISettings* settings, const QString& screenId)
{
    // Per-screen snapping override (highest priority), with virtual screen fallback
    if (!screenId.isEmpty() && settings) {
        QVariantMap perScreen = getPerScreenSnappingWithFallback(settings, screenId);
        auto it = perScreen.constFind(QLatin1String(PerScreenSnappingKey::ZonePadding));
        if (it != perScreen.constEnd()) {
            return it->toInt();
        }
    }
    // Check for layout-specific override
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

QRect snapToRect(const QRectF& rf)
{
    return GeometryHelpers::snapToRect(rf);
}

::PhosphorLayout::EdgeGaps getEffectiveOuterGaps(Layout* layout, ISettings* settings, const QString& screenId)
{
    // Per-screen snapping override (highest priority), with virtual screen fallback
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
                // If any per-side key is present, use per-screen per-side gaps
                if (topIt != perScreen.constEnd() || bottomIt != perScreen.constEnd() || leftIt != perScreen.constEnd()
                    || rightIt != perScreen.constEnd()) {
                    // Fall back to per-screen uniform OuterGap, then global for missing sides
                    auto uniformIt = perScreen.constFind(QLatin1String(PerScreenSnappingKey::OuterGap));
                    int fallback = (uniformIt != perScreen.constEnd()) ? uniformIt->toInt() : settings->outerGap();
                    return {(topIt != perScreen.constEnd()) ? topIt->toInt() : fallback,
                            (bottomIt != perScreen.constEnd()) ? bottomIt->toInt() : fallback,
                            (leftIt != perScreen.constEnd()) ? leftIt->toInt() : fallback,
                            (rightIt != perScreen.constEnd()) ? rightIt->toInt() : fallback};
                }
            }
            // UsePerSideOuterGap not set or false — per-side keys ignored if present
            // Per-screen uniform outer gap
            auto uniformIt = perScreen.constFind(QLatin1String(PerScreenSnappingKey::OuterGap));
            if (uniformIt != perScreen.constEnd()) {
                return ::PhosphorLayout::EdgeGaps::uniform(uniformIt->toInt());
            }
        }
    }

    // Check for layout-specific per-side override first
    if (layout && layout->usePerSideOuterGap() && layout->hasPerSideOuterGapOverride()) {
        ::PhosphorLayout::EdgeGaps gaps = layout->rawOuterGaps();
        // Fill in -1 sentinel values from global per-side or uniform fallback
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
            int fallback = settings ? settings->outerGap() : Defaults::OuterGap;
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

    // Check for layout-specific uniform override
    if (layout && layout->hasOuterGapOverride()) {
        return ::PhosphorLayout::EdgeGaps::uniform(layout->outerGap());
    }

    // Fall back to global settings
    if (settings) {
        if (settings->usePerSideOuterGap()) {
            return {settings->outerGapTop(), settings->outerGapBottom(), settings->outerGapLeft(),
                    settings->outerGapRight()};
        }
        return ::PhosphorLayout::EdgeGaps::uniform(settings->outerGap());
    }

    return ::PhosphorLayout::EdgeGaps::uniform(Defaults::OuterGap);
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

QRectF effectiveScreenGeometry(Layout* layout, const QString& screenId)
{
    auto [geom, availGeom] = resolveScreenGeometries(screenId);
    if (geom.isValid()) {
        if (layout && layout->useFullScreenGeometry()) {
            return QRectF(geom);
        }
        return availGeom.isValid() ? QRectF(availGeom) : QRectF(geom);
    }
    // Fallback to physical screen
    QScreen* screen = Utils::findScreenByIdOrName(screenId);
    return effectiveScreenGeometry(layout, screen);
}

QRectF extractZoneGeometry(const QVariantMap& zone)
{
    return QRectF(zone.value(JsonKeys::X).toDouble(), zone.value(JsonKeys::Y).toDouble(),
                  zone.value(JsonKeys::Width).toDouble(), zone.value(JsonKeys::Height).toDouble());
}

void setZoneGeometry(QVariantMap& zone, const QRectF& rect)
{
    zone[JsonKeys::X] = rect.x();
    zone[JsonKeys::Y] = rect.y();
    zone[JsonKeys::Width] = rect.width();
    zone[JsonKeys::Height] = rect.height();
}

/// Iterates empty zones and builds EmptyZoneEntry values for the typed D-Bus result.
/// EmptyZoneEntry values. Accepts either explicit screen geometry (virtual screen path)
/// or a physical QScreen* (physical screen path).
static EmptyZoneList buildEmptyZoneListImpl(Layout* layout, const std::optional<QRect>& screenGeometry,
                                            const QRect& availableGeometry, const QString& screenId, int zonePadding,
                                            const ::PhosphorLayout::EdgeGaps& outerGaps, bool useAvail,
                                            ISettings* settings, const QRect& overlayOriginRect, QScreen* physScreen,
                                            const std::function<bool(const Zone*)>& isZoneEmpty)
{
    QRect resolvedScreenGeom;
    QRect resolvedAvailGeom;
    QRect resolvedOverlayOrigin;
    if (screenGeometry.has_value()) {
        resolvedScreenGeom = *screenGeometry;
        resolvedAvailGeom = availableGeometry.isValid() ? availableGeometry : resolvedScreenGeom;
        resolvedOverlayOrigin = overlayOriginRect;
    } else if (physScreen) {
        resolvedScreenGeom = physScreen->geometry();
        resolvedAvailGeom = ScreenManager::actualAvailableGeometry(physScreen);
        resolvedOverlayOrigin = physScreen->geometry();
    } else {
        return {};
    }

    EmptyZoneList result;
    for (Zone* zone : layout->zones()) {
        if (!isZoneEmpty(zone)) {
            continue;
        }
        QRectF geom = getZoneGeometryWithGaps(zone, resolvedScreenGeom, resolvedAvailGeom, zonePadding, outerGaps,
                                              useAvail, screenId);
        QRect overlayGeom = snapToRect(availableAreaToOverlayCoordinates(geom, resolvedOverlayOrigin));

        int bw = zone->useCustomColors() ? zone->borderWidth()
                                         : (settings ? settings->borderWidth() : Defaults::BorderWidth);
        int br = zone->useCustomColors() ? zone->borderRadius()
                                         : (settings ? settings->borderRadius() : Defaults::BorderRadius);

        EmptyZoneEntry entry;
        entry.zoneId = zone->id().toString();
        entry.x = overlayGeom.x();
        entry.y = overlayGeom.y();
        entry.width = overlayGeom.width();
        entry.height = overlayGeom.height();
        entry.borderWidth = bw;
        entry.borderRadius = br;
        entry.useCustomColors = zone->useCustomColors();
        if (zone->useCustomColors()) {
            entry.highlightColor = zone->highlightColor().name(QColor::HexArgb);
            entry.inactiveColor = zone->inactiveColor().name(QColor::HexArgb);
            entry.borderColor = zone->borderColor().name(QColor::HexArgb);
            entry.activeOpacity = zone->activeOpacity();
            entry.inactiveOpacity = zone->inactiveOpacity();
        }
        result.append(entry);
    }
    return result;
}

EmptyZoneList buildEmptyZoneList(Layout* layout, QScreen* screen, ISettings* settings,
                                 const std::function<bool(const Zone*)>& isZoneEmpty)
{
    if (!layout || !screen) {
        return {};
    }

    bool useAvail = !layout->useFullScreenGeometry();
    LayoutComputeService::recalculateSync(layout, effectiveScreenGeometry(layout, screen));

    QString screenId = Utils::screenIdentifier(screen);
    int zonePadding = getEffectiveZonePadding(layout, settings, screenId);
    ::PhosphorLayout::EdgeGaps outerGaps = getEffectiveOuterGaps(layout, settings, screenId);

    return buildEmptyZoneListImpl(layout, std::nullopt, QRect(), screenId, zonePadding, outerGaps, useAvail, settings,
                                  QRect(), screen, isZoneEmpty);
}

EmptyZoneList buildEmptyZoneList(Layout* layout, const QString& screenId, QScreen* physScreen, ISettings* settings,
                                 const std::function<bool(const Zone*)>& isZoneEmpty)
{
    if (!layout) {
        return {};
    }

    auto [vsGeom, vsAvailGeom] = resolveScreenGeometries(screenId);

    if (vsGeom.isValid()) {
        bool useAvail = !layout->useFullScreenGeometry();
        QRectF effectiveGeom = useAvail && vsAvailGeom.isValid() ? QRectF(vsAvailGeom) : QRectF(vsGeom);
        LayoutComputeService::recalculateSync(layout, effectiveGeom);

        int zonePadding = getEffectiveZonePadding(layout, settings, screenId);
        ::PhosphorLayout::EdgeGaps outerGaps = getEffectiveOuterGaps(layout, settings, screenId);

        return buildEmptyZoneListImpl(layout, std::optional<QRect>(vsGeom), vsAvailGeom, screenId, zonePadding,
                                      outerGaps, useAvail, settings, vsGeom, nullptr, isZoneEmpty);
    }

    return physScreen ? buildEmptyZoneList(layout, physScreen, settings, isZoneEmpty) : EmptyZoneList{};
}

QString rectToJson(const QRect& rect)
{
    QJsonObject obj;
    obj[JsonKeys::X] = rect.x();
    obj[JsonKeys::Y] = rect.y();
    obj[JsonKeys::Width] = rect.width();
    obj[JsonKeys::Height] = rect.height();
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
        obj[JsonKeys::WindowId] = entry.windowId;
        obj[JsonKeys::SourceZoneId] = entry.sourceZoneId;
        obj[JsonKeys::TargetZoneId] = entry.targetZoneId;
        if (!entry.targetZoneIds.isEmpty()) {
            QJsonArray zoneIdsArr;
            for (const QString& zid : entry.targetZoneIds)
                zoneIdsArr.append(zid);
            obj[JsonKeys::TargetZoneIds] = zoneIdsArr;
        }
        obj[JsonKeys::X] = entry.targetGeometry.x();
        obj[JsonKeys::Y] = entry.targetGeometry.y();
        obj[JsonKeys::Width] = entry.targetGeometry.width();
        obj[JsonKeys::Height] = entry.targetGeometry.height();
        array.append(obj);
    }
    return QString::fromUtf8(QJsonDocument(array).toJson(QJsonDocument::Compact));
}

} // namespace GeometryUtils

} // namespace PlasmaZones
