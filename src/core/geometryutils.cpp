// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "geometryutils.h"
#include <PhosphorZones/Zone.h>
#include <PhosphorZones/Layout.h>
#include <PhosphorZones/LayoutComputeService.h>
#include <PhosphorZones/AssignmentEntry.h>
#include <PhosphorZones/IZoneLayoutRegistry.h>
#include "interfaces.h"
#include "constants.h"
#include <PhosphorEngine/IGeometrySettings.h>
#include <PhosphorIdentity/VirtualScreenId.h>
#include <PhosphorScreens/Manager.h>
#include <PhosphorScreens/ScreenIdentity.h>
#include <PhosphorZones/ZoneDefaults.h>
#include <QScreen>

static_assert(PhosphorEngine::GeometryDefaults::ZonePadding == PlasmaZones::Defaults::ZonePadding,
              "Library and daemon ZonePadding defaults out of sync");
static_assert(PhosphorEngine::GeometryDefaults::OuterGap == PlasmaZones::Defaults::OuterGap,
              "Library and daemon OuterGap defaults out of sync");

namespace PlasmaZones {

namespace GeometryUtils {

namespace {
struct ScreenGeometries
{
    QRect geometry;
    QRect availableGeometry;
};

ScreenGeometries resolveScreenGeometries(PhosphorScreens::ScreenManager* mgr, const QString& screenId)
{
    if (!mgr) {
        return {};
    }
    return {mgr->screenGeometry(screenId), mgr->screenAvailableGeometry(screenId)};
}

QVariantMap getPerScreenSnappingWithFallback(PhosphorEngine::IGeometrySettings* settings, const QString& screenId)
{
    QVariantMap result = settings->getPerScreenSnappingSettings(screenId);
    if (result.isEmpty() && PhosphorIdentity::VirtualScreenId::isVirtual(screenId)) {
        result = settings->getPerScreenSnappingSettings(PhosphorIdentity::VirtualScreenId::extractPhysicalId(screenId));
    }
    return result;
}

// Resolve outer gaps from a PerScreenSnappingKey-shaped override map (used for
// BOTH the context-rule layer and the per-screen layer). Returns nullopt when
// the map carries no outer-gap info so the caller falls through to the next
// precedence layer. A partial per-side map fills missing sides from the map's
// own uniform OuterGap or, failing that, the global setting — preserving the
// historical per-screen semantics now that the rule layer shares this path.
std::optional<::PhosphorLayout::EdgeGaps> resolveOuterGapsFromMap(const QVariantMap& map,
                                                                  PhosphorEngine::IGeometrySettings* settings)
{
    namespace PSK = PhosphorEngine::PerScreenSnappingKey;
    namespace GD = PhosphorEngine::GeometryDefaults;
    if (map.isEmpty()) {
        return std::nullopt;
    }
    auto usePerSideIt = map.constFind(PSK::UsePerSideOuterGap);
    const bool usePerSide = (usePerSideIt != map.constEnd()) ? usePerSideIt->toBool() : false;
    if (usePerSide) {
        auto topIt = map.constFind(PSK::OuterGapTop);
        auto bottomIt = map.constFind(PSK::OuterGapBottom);
        auto leftIt = map.constFind(PSK::OuterGapLeft);
        auto rightIt = map.constFind(PSK::OuterGapRight);
        if (topIt != map.constEnd() || bottomIt != map.constEnd() || leftIt != map.constEnd()
            || rightIt != map.constEnd()) {
            auto uniformIt = map.constFind(PSK::OuterGap);
            const int fallback = (uniformIt != map.constEnd()) ? uniformIt->toInt()
                : settings                                     ? settings->outerGap()
                                                               : GD::OuterGap;
            return ::PhosphorLayout::EdgeGaps{(topIt != map.constEnd()) ? topIt->toInt() : fallback,
                                              (bottomIt != map.constEnd()) ? bottomIt->toInt() : fallback,
                                              (leftIt != map.constEnd()) ? leftIt->toInt() : fallback,
                                              (rightIt != map.constEnd()) ? rightIt->toInt() : fallback};
        }
    }
    auto uniformIt = map.constFind(PSK::OuterGap);
    if (uniformIt != map.constEnd()) {
        return ::PhosphorLayout::EdgeGaps::uniform(uniformIt->toInt());
    }
    return std::nullopt;
}

// Resolve the context-rule gap override for @p screenId in @p reg's current
// desktop/activity, as the PerScreenSnappingKey-shaped map the getEffective*
// helpers consume. Returns an empty map when no registry / no matching rule —
// the callers then fall through to the per-screen/layout/global cascade exactly
// as before. The registry's current context is kept in sync with the
// compositor by the daemon (start.cpp / signals.cpp), so the override matches
// the one DaemonGeometryResolver computes on the commit path.
QVariantMap currentContextGapOverride(PhosphorZones::IZoneLayoutRegistry* reg, const QString& screenId)
{
    if (!reg || screenId.isEmpty()) {
        return {};
    }
    return contextGapOverrideMap(
        reg->resolveContextGaps(screenId, reg->currentVirtualDesktopForScreen(screenId), reg->currentActivity()));
}
} // anonymous namespace

QVariantMap contextGapOverrideMap(const PhosphorZones::ContextGapOverride& gaps)
{
    namespace PSK = PhosphorEngine::PerScreenSnappingKey;
    QVariantMap map;
    if (gaps.isEmpty()) {
        return map;
    }
    if (gaps.zonePadding) {
        map.insert(QString(PSK::ZonePadding), *gaps.zonePadding);
    }
    if (gaps.outerGap) {
        map.insert(QString(PSK::OuterGap), *gaps.outerGap);
    }
    if (gaps.usePerSideOuterGap) {
        map.insert(QString(PSK::UsePerSideOuterGap), *gaps.usePerSideOuterGap);
    }
    if (gaps.outerGapTop) {
        map.insert(QString(PSK::OuterGapTop), *gaps.outerGapTop);
    }
    if (gaps.outerGapBottom) {
        map.insert(QString(PSK::OuterGapBottom), *gaps.outerGapBottom);
    }
    if (gaps.outerGapLeft) {
        map.insert(QString(PSK::OuterGapLeft), *gaps.outerGapLeft);
    }
    if (gaps.outerGapRight) {
        map.insert(QString(PSK::OuterGapRight), *gaps.outerGapRight);
    }
    return map;
}

int getEffectiveZonePadding(PhosphorZones::Layout* layout, ISettings* settings, const QString& screenId,
                            const QVariantMap& ruleGapOverride, GapLayer* winningLayer)
{
    namespace PSK = PhosphorEngine::PerScreenSnappingKey;
    const auto mark = [winningLayer](GapLayer layer) {
        if (winningLayer)
            *winningLayer = layer;
    };
    // Highest precedence: a context-rule gap override for this context.
    {
        auto it = ruleGapOverride.constFind(PSK::ZonePadding);
        if (it != ruleGapOverride.constEnd()) {
            mark(GapLayer::ContextRule);
            return it->toInt();
        }
    }
    if (!screenId.isEmpty() && settings) {
        QVariantMap perScreen = getPerScreenSnappingWithFallback(settings, screenId);
        auto it = perScreen.constFind(PSK::ZonePadding);
        if (it != perScreen.constEnd()) {
            mark(GapLayer::PerScreen);
            return it->toInt();
        }
    }
    if (layout && layout->hasZonePaddingOverride()) {
        mark(GapLayer::Layout);
        return layout->zonePadding();
    }
    if (settings) {
        mark(GapLayer::Global);
        return settings->zonePadding();
    }
    mark(GapLayer::Default);
    return PhosphorEngine::GeometryDefaults::ZonePadding;
}

::PhosphorLayout::EdgeGaps getEffectiveOuterGaps(PhosphorZones::Layout* layout, ISettings* settings,
                                                 const QString& screenId, const QVariantMap& ruleGapOverride,
                                                 GapLayer* winningLayer)
{
    namespace GD = PhosphorEngine::GeometryDefaults;
    const auto mark = [winningLayer](GapLayer layer) {
        if (winningLayer)
            *winningLayer = layer;
    };
    // Highest precedence: a context-rule gap override for this context.
    if (auto ruleGaps = resolveOuterGapsFromMap(ruleGapOverride, settings)) {
        mark(GapLayer::ContextRule);
        return *ruleGaps;
    }
    if (!screenId.isEmpty() && settings) {
        QVariantMap perScreen = getPerScreenSnappingWithFallback(settings, screenId);
        if (auto perScreenGaps = resolveOuterGapsFromMap(perScreen, settings)) {
            mark(GapLayer::PerScreen);
            return *perScreenGaps;
        }
    }
    if (layout && layout->usePerSideOuterGap() && layout->hasPerSideOuterGapOverride()) {
        mark(GapLayer::Layout);
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
            int fallback = settings ? settings->outerGap() : GD::OuterGap;
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
        mark(GapLayer::Layout);
        return ::PhosphorLayout::EdgeGaps::uniform(layout->outerGap());
    }
    if (settings) {
        mark(GapLayer::Global);
        if (settings->usePerSideOuterGap()) {
            return {settings->outerGapTop(), settings->outerGapBottom(), settings->outerGapLeft(),
                    settings->outerGapRight()};
        }
        return ::PhosphorLayout::EdgeGaps::uniform(settings->outerGap());
    }
    mark(GapLayer::Default);
    return ::PhosphorLayout::EdgeGaps::uniform(GD::OuterGap);
}

QRectF getZoneGeometryForScreenF(PhosphorScreens::ScreenManager* mgr, PhosphorZones::Zone* zone, QScreen* screen,
                                 const QString& screenId, PhosphorZones::Layout* layout, ISettings* settings,
                                 PhosphorZones::IZoneLayoutRegistry* layoutRegistry)
{
    const QVariantMap ruleGaps = currentContextGapOverride(layoutRegistry, screenId);
    int zp = getEffectiveZonePadding(layout, settings, screenId, ruleGaps);
    ::PhosphorLayout::EdgeGaps og = getEffectiveOuterGaps(layout, settings, screenId, ruleGaps);
    return ::PhosphorZones::GeometryUtils::getZoneGeometryForScreenF(mgr, zone, screen, screenId, layout, zp, og);
}

QRect getZoneGeometryForScreen(PhosphorScreens::ScreenManager* mgr, PhosphorZones::Zone* zone, QScreen* screen,
                               const QString& screenId, PhosphorZones::Layout* layout, ISettings* settings,
                               PhosphorZones::IZoneLayoutRegistry* layoutRegistry)
{
    const QVariantMap ruleGaps = currentContextGapOverride(layoutRegistry, screenId);
    int zp = getEffectiveZonePadding(layout, settings, screenId, ruleGaps);
    ::PhosphorLayout::EdgeGaps og = getEffectiveOuterGaps(layout, settings, screenId, ruleGaps);
    return ::PhosphorZones::GeometryUtils::getZoneGeometryForScreen(mgr, zone, screen, screenId, layout, zp, og);
}

static PhosphorProtocol::EmptyZoneList
buildEmptyZoneListImpl(PhosphorScreens::ScreenManager* mgr, PhosphorZones::Layout* layout,
                       const std::optional<QRect>& screenGeometry, const QRect& availableGeometry,
                       const QString& screenId, int zonePadding, const ::PhosphorLayout::EdgeGaps& outerGaps,
                       bool useAvail, ISettings* settings, const QRect& overlayOriginRect, QScreen* physScreen,
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
        // Mirror the screenGeometry branch above: never let an invalid
        // available rect through. actualAvailableGeometry(QScreen*) already
        // falls back to QScreen::availableGeometry(); guard the result so
        // empty-zone geometry is never computed against an empty reference.
        const QRect avail = mgr ? mgr->actualAvailableGeometry(physScreen) : physScreen->availableGeometry();
        resolvedAvailGeom = avail.isValid() ? avail : resolvedScreenGeom;
        resolvedOverlayOrigin = physScreen->geometry();
    } else {
        return {};
    }

    PhosphorProtocol::EmptyZoneList result;
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

        PhosphorProtocol::EmptyZoneEntry entry;
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

PhosphorProtocol::EmptyZoneList buildEmptyZoneList(PhosphorScreens::ScreenManager* mgr, PhosphorZones::Layout* layout,
                                                   QScreen* screen, ISettings* settings,
                                                   const std::function<bool(const PhosphorZones::Zone*)>& isZoneEmpty,
                                                   PhosphorZones::IZoneLayoutRegistry* layoutRegistry)
{
    if (!layout || !screen) {
        return {};
    }

    bool useAvail = !layout->useFullScreenGeometry();
    PhosphorZones::LayoutComputeService::recalculateSync(layout, effectiveScreenGeometry(mgr, layout, screen));

    QString screenId = PhosphorScreens::ScreenIdentity::identifierFor(screen);
    const QVariantMap ruleGaps = currentContextGapOverride(layoutRegistry, screenId);
    int zonePadding = getEffectiveZonePadding(layout, settings, screenId, ruleGaps);
    ::PhosphorLayout::EdgeGaps outerGaps = getEffectiveOuterGaps(layout, settings, screenId, ruleGaps);

    return buildEmptyZoneListImpl(mgr, layout, std::nullopt, QRect(), screenId, zonePadding, outerGaps, useAvail,
                                  settings, QRect(), screen, isZoneEmpty);
}

PhosphorProtocol::EmptyZoneList buildEmptyZoneList(PhosphorScreens::ScreenManager* mgr, PhosphorZones::Layout* layout,
                                                   const QString& screenId, QScreen* physScreen, ISettings* settings,
                                                   const std::function<bool(const PhosphorZones::Zone*)>& isZoneEmpty,
                                                   PhosphorZones::IZoneLayoutRegistry* layoutRegistry)
{
    if (!layout) {
        return {};
    }

    auto [vsGeom, vsAvailGeom] = resolveScreenGeometries(mgr, screenId);

    if (vsGeom.isValid()) {
        bool useAvail = !layout->useFullScreenGeometry();
        QRectF effectiveGeom = useAvail && vsAvailGeom.isValid() ? QRectF(vsAvailGeom) : QRectF(vsGeom);
        PhosphorZones::LayoutComputeService::recalculateSync(layout, effectiveGeom);

        const QVariantMap ruleGaps = currentContextGapOverride(layoutRegistry, screenId);
        int zonePadding = getEffectiveZonePadding(layout, settings, screenId, ruleGaps);
        ::PhosphorLayout::EdgeGaps outerGaps = getEffectiveOuterGaps(layout, settings, screenId, ruleGaps);

        return buildEmptyZoneListImpl(mgr, layout, std::optional<QRect>(vsGeom), vsAvailGeom, screenId, zonePadding,
                                      outerGaps, useAvail, settings, vsGeom, nullptr, isZoneEmpty);
    }

    return physScreen ? buildEmptyZoneList(mgr, layout, physScreen, settings, isZoneEmpty, layoutRegistry)
                      : PhosphorProtocol::EmptyZoneList{};
}

} // namespace GeometryUtils

} // namespace PlasmaZones
