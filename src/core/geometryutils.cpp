// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "geometryutils.h"
#include <PhosphorZones/Zone.h>
#include <geometry_helpers.h>
#include <PhosphorZones/Layout.h>
#include "layoutworker/layoutcomputeservice.h"
#include "interfaces.h"
#include "constants.h"
#include <PhosphorScreens/Manager.h>
#include <QScreen>
#include <QVariantMap>
#include <PhosphorScreens/ScreenIdentity.h>
#include <PhosphorZones/ZoneDefaults.h>

namespace PlasmaZones {

static QVariantMap getPerScreenSnappingWithFallback(ISettings* settings, const QString& screenId)
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
} // anonymous namespace

QRectF getZoneGeometryForScreenF(Phosphor::Screens::ScreenManager* mgr, PhosphorZones::Zone* zone, QScreen* screen,
                                 const QString& screenId, PhosphorZones::Layout* layout, ISettings* settings)
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
                               const QString& screenId, PhosphorZones::Layout* layout, ISettings* settings)
{
    QRectF geoF = getZoneGeometryForScreenF(mgr, zone, screen, screenId, layout, settings);
    return geoF.isValid() ? snapToRect(geoF) : QRect();
}

int getEffectiveZonePadding(PhosphorZones::Layout* layout, ISettings* settings, const QString& screenId)
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
    return Defaults::ZonePadding;
}

::PhosphorLayout::EdgeGaps getEffectiveOuterGaps(PhosphorZones::Layout* layout, ISettings* settings,
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

    return ::PhosphorLayout::EdgeGaps::uniform(Defaults::OuterGap);
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
