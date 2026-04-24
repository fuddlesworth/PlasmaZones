// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "geometryutils.h"
#include <PhosphorZones/Zone.h>
#include <PhosphorZones/Layout.h>
#include "layoutworker/layoutcomputeservice.h"
#include "interfaces.h"
#include "constants.h"
#include <PhosphorEngineApi/IGeometrySettings.h>
#include <PhosphorScreens/Manager.h>
#include <PhosphorScreens/ScreenIdentity.h>
#include <PhosphorZones/ZoneDefaults.h>
#include <QScreen>

static_assert(PhosphorEngineApi::GeometryDefaults::ZonePadding == PlasmaZones::Defaults::ZonePadding,
              "Library and daemon ZonePadding defaults out of sync");
static_assert(PhosphorEngineApi::GeometryDefaults::OuterGap == PlasmaZones::Defaults::OuterGap,
              "Library and daemon OuterGap defaults out of sync");

namespace PlasmaZones {

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
} // anonymous namespace

QRectF getZoneGeometryForScreenF(Phosphor::Screens::ScreenManager* mgr, PhosphorZones::Zone* zone, QScreen* screen,
                                 const QString& screenId, PhosphorZones::Layout* layout, ISettings* settings)
{
    return ::PhosphorZones::GeometryUtils::getZoneGeometryForScreenF(mgr, zone, screen, screenId, layout, settings);
}

QRect getZoneGeometryForScreen(Phosphor::Screens::ScreenManager* mgr, PhosphorZones::Zone* zone, QScreen* screen,
                               const QString& screenId, PhosphorZones::Layout* layout, ISettings* settings)
{
    return ::PhosphorZones::GeometryUtils::getZoneGeometryForScreen(mgr, zone, screen, screenId, layout, settings);
}

int getEffectiveZonePadding(PhosphorZones::Layout* layout, ISettings* settings, const QString& screenId)
{
    return ::PhosphorZones::GeometryUtils::getEffectiveZonePadding(layout, settings, screenId);
}

::PhosphorLayout::EdgeGaps getEffectiveOuterGaps(PhosphorZones::Layout* layout, ISettings* settings,
                                                 const QString& screenId)
{
    return ::PhosphorZones::GeometryUtils::getEffectiveOuterGaps(layout, settings, screenId);
}

static EmptyZoneList buildEmptyZoneListImpl(Phosphor::Screens::ScreenManager* mgr, PhosphorZones::Layout* layout,
                                            const std::optional<QRect>& screenGeometry, const QRect& availableGeometry,
                                            const QString& screenId, int zonePadding,
                                            const ::PhosphorLayout::EdgeGaps& outerGaps, bool useAvail,
                                            ISettings* settings, const QRect& overlayOriginRect, QScreen* physScreen,
                                            const std::function<bool(const PhosphorZones::Zone*)>& isZoneEmpty)
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
        resolvedAvailGeom = mgr ? mgr->actualAvailableGeometry(physScreen) : physScreen->availableGeometry();
        resolvedOverlayOrigin = physScreen->geometry();
    } else {
        return {};
    }

    EmptyZoneList result;
    for (PhosphorZones::Zone* zone : layout->zones()) {
        if (!isZoneEmpty(zone)) {
            continue;
        }
        QRectF geom = getZoneGeometryWithGaps(mgr, zone, resolvedScreenGeom, resolvedAvailGeom, zonePadding, outerGaps,
                                              useAvail, screenId);
        QRect overlayGeom = snapToRect(availableAreaToOverlayCoordinates(geom, resolvedOverlayOrigin));

        int bw = zone->useCustomColors()
            ? zone->borderWidth()
            : (settings ? settings->borderWidth() : ::PhosphorZones::ZoneDefaults::BorderWidth);
        int br = zone->useCustomColors()
            ? zone->borderRadius()
            : (settings ? settings->borderRadius() : ::PhosphorZones::ZoneDefaults::BorderRadius);

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

EmptyZoneList buildEmptyZoneList(Phosphor::Screens::ScreenManager* mgr, PhosphorZones::Layout* layout, QScreen* screen,
                                 ISettings* settings,
                                 const std::function<bool(const PhosphorZones::Zone*)>& isZoneEmpty)
{
    if (!layout || !screen) {
        return {};
    }

    bool useAvail = !layout->useFullScreenGeometry();
    LayoutComputeService::recalculateSync(layout, effectiveScreenGeometry(mgr, layout, screen));

    QString screenId = Phosphor::Screens::ScreenIdentity::identifierFor(screen);
    int zonePadding = getEffectiveZonePadding(layout, settings, screenId);
    ::PhosphorLayout::EdgeGaps outerGaps = getEffectiveOuterGaps(layout, settings, screenId);

    return buildEmptyZoneListImpl(mgr, layout, std::nullopt, QRect(), screenId, zonePadding, outerGaps, useAvail,
                                  settings, QRect(), screen, isZoneEmpty);
}

EmptyZoneList buildEmptyZoneList(Phosphor::Screens::ScreenManager* mgr, PhosphorZones::Layout* layout,
                                 const QString& screenId, QScreen* physScreen, ISettings* settings,
                                 const std::function<bool(const PhosphorZones::Zone*)>& isZoneEmpty)
{
    if (!layout) {
        return {};
    }

    auto [vsGeom, vsAvailGeom] = resolveScreenGeometries(mgr, screenId);

    if (vsGeom.isValid()) {
        bool useAvail = !layout->useFullScreenGeometry();
        QRectF effectiveGeom = useAvail && vsAvailGeom.isValid() ? QRectF(vsAvailGeom) : QRectF(vsGeom);
        LayoutComputeService::recalculateSync(layout, effectiveGeom);

        int zonePadding = getEffectiveZonePadding(layout, settings, screenId);
        ::PhosphorLayout::EdgeGaps outerGaps = getEffectiveOuterGaps(layout, settings, screenId);

        return buildEmptyZoneListImpl(mgr, layout, std::optional<QRect>(vsGeom), vsAvailGeom, screenId, zonePadding,
                                      outerGaps, useAvail, settings, vsGeom, nullptr, isZoneEmpty);
    }

    return physScreen ? buildEmptyZoneList(mgr, layout, physScreen, settings, isZoneEmpty) : EmptyZoneList{};
}

} // namespace GeometryUtils

} // namespace PlasmaZones
