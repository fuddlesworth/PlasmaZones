// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "geometryutils.h"
#include <PhosphorZones/Zone.h>
#include <PhosphorZones/Layout.h>
#include <PhosphorZones/LayoutComputeService.h>
#include <PhosphorZones/AssignmentEntry.h>
#include <PhosphorZones/IZoneLayoutRegistry.h>
#include "core/interfaces/interfaces.h"
#include "core/types/constants.h"
#include <PhosphorEngine/GapResolution.h>
#include <PhosphorEngine/IGeometrySettings.h>
#include <PhosphorEngine/PerScreenKeys.h>
#include <PhosphorScreens/Manager.h>
#include <PhosphorScreens/ScreenIdentity.h>
#include <PhosphorZones/ZoneDefaults.h>
#include <QScreen>

static_assert(PhosphorEngine::GeometryDefaults::InnerGap == PlasmaZones::Defaults::InnerGap,
              "Library and daemon InnerGap defaults out of sync");
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

// Resolve outer gaps from a PerScreenKeys-shaped override map (the
// context-rule gap override) via the shared atomic-layer resolution in
// PhosphorEngine::GapResolution. Snapping consumes raw map values (identity
// normalize); missing sides in a partial per-side map fall back to the map's
// own uniform OuterGap or, failing that, the global setting.
std::optional<::PhosphorLayout::EdgeGaps> resolveOuterGapsFromMap(const QVariantMap& map,
                                                                  PhosphorEngine::IGeometrySettings* settings)
{
    namespace GD = PhosphorEngine::GeometryDefaults;
    const int base = settings ? settings->outerGap() : GD::OuterGap;
    return PhosphorEngine::GapResolution::outerGapsFromOverrideMap(map, base, [](int v) {
        return v;
    });
}

} // anonymous namespace

QVariantMap mergeConfigPerScreenGaps(QVariantMap ruleOverride, const ISettings* settings, const QString& screenId)
{
    QVariantMap merged = settings ? settings->perScreenGapOverrides(screenId) : QVariantMap{};
    // Rule wins per key: insert the rule override on top of the config base.
    for (auto it = ruleOverride.constBegin(); it != ruleOverride.constEnd(); ++it) {
        merged.insert(it.key(), it.value());
    }
    return merged;
}

QVariantMap currentContextGapOverride(PhosphorZones::IZoneLayoutRegistry* reg, const ISettings* settings,
                                      const QString& screenId)
{
    if (screenId.isEmpty()) {
        return {};
    }
    // The preview / query / empty-zone geometry helpers that call this are all
    // snapping-side, so resolve against the "snapping" placement mode — the
    // preview geometry then matches the snap-commit path (DaemonGeometryResolver
    // passes the same token). The config per-monitor gap is merged UNDER the rule
    // override so a user gap rule still wins per slot.
    QVariantMap ruleGaps = reg
        ? contextGapOverrideMap(reg->resolveContextGaps(screenId, reg->currentVirtualDesktopForScreen(screenId),
                                                        reg->currentActivity(), QStringLiteral("snapping")))
        : QVariantMap{};
    return mergeConfigPerScreenGaps(std::move(ruleGaps), settings, screenId);
}

QVariantMap contextGapOverrideMap(const PhosphorZones::ContextGapOverride& gaps)
{
    // This map is consumed by BOTH engines; both read the single shared
    // PhosphorEngine::PerScreenKeys namespace, so the key strings agree by
    // construction.
    namespace PSK = PhosphorEngine::PerScreenKeys;
    QVariantMap map;
    if (gaps.isEmpty()) {
        return map;
    }
    if (gaps.innerGap) {
        map.insert(QString(PSK::InnerGap), *gaps.innerGap);
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

int getEffectiveInnerGap(PhosphorZones::Layout* layout, ISettings* settings, const QVariantMap& ruleGapOverride)
{
    namespace PSK = PhosphorEngine::PerScreenKeys;
    // Tier 1 — a context-rule gap override (per-screen/desktop/activity rule).
    // Same shared helper the autotile resolver uses; snapping consumes the raw
    // value (identity normalize).
    if (auto v = PhosphorEngine::GapResolution::gapFromOverrideMap(ruleGapOverride, PSK::InnerGap, [](int g) {
            return g;
        })) {
        return *v;
    }
    // Tier 2 — per-layout override.
    if (layout && layout->hasZonePaddingOverride()) {
        return layout->zonePadding();
    }
    // Tier 3 — global default (settings->innerGap() reads the Gaps config group).
    if (settings) {
        return settings->innerGap();
    }
    // Tier 4 — compile default.
    return PhosphorEngine::GeometryDefaults::InnerGap;
}

::PhosphorLayout::EdgeGaps getEffectiveOuterGaps(PhosphorZones::Layout* layout, ISettings* settings,
                                                 const QVariantMap& ruleGapOverride)
{
    namespace GD = PhosphorEngine::GeometryDefaults;
    // Tier 1 — a context-rule gap override (per-screen/desktop/activity rule).
    if (auto ruleGaps = resolveOuterGapsFromMap(ruleGapOverride, settings)) {
        return *ruleGaps;
    }
    // Tier 2 — per-layout override.
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
        return ::PhosphorLayout::EdgeGaps::uniform(layout->outerGap());
    }
    if (settings) {
        if (settings->usePerSideOuterGap()) {
            return {settings->outerGapTop(), settings->outerGapBottom(), settings->outerGapLeft(),
                    settings->outerGapRight()};
        }
        return ::PhosphorLayout::EdgeGaps::uniform(settings->outerGap());
    }
    return ::PhosphorLayout::EdgeGaps::uniform(GD::OuterGap);
}

QRectF getZoneGeometryForScreenF(PhosphorScreens::ScreenManager* mgr, PhosphorZones::Zone* zone, QScreen* screen,
                                 const QString& screenId, PhosphorZones::Layout* layout, ISettings* settings,
                                 PhosphorZones::IZoneLayoutRegistry* layoutRegistry)
{
    const QVariantMap ruleGaps = currentContextGapOverride(layoutRegistry, settings, screenId);
    int zp = getEffectiveInnerGap(layout, settings, ruleGaps);
    ::PhosphorLayout::EdgeGaps og = getEffectiveOuterGaps(layout, settings, ruleGaps);
    return ::PhosphorZones::GeometryUtils::getZoneGeometryForScreenF(mgr, zone, screen, screenId, layout, zp, og);
}

QRect getZoneGeometryForScreen(PhosphorScreens::ScreenManager* mgr, PhosphorZones::Zone* zone, QScreen* screen,
                               const QString& screenId, PhosphorZones::Layout* layout, ISettings* settings,
                               PhosphorZones::IZoneLayoutRegistry* layoutRegistry)
{
    const QVariantMap ruleGaps = currentContextGapOverride(layoutRegistry, settings, screenId);
    int zp = getEffectiveInnerGap(layout, settings, ruleGaps);
    ::PhosphorLayout::EdgeGaps og = getEffectiveOuterGaps(layout, settings, ruleGaps);
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
    const QVariantMap ruleGaps = currentContextGapOverride(layoutRegistry, settings, screenId);
    int zonePadding = getEffectiveInnerGap(layout, settings, ruleGaps);
    ::PhosphorLayout::EdgeGaps outerGaps = getEffectiveOuterGaps(layout, settings, ruleGaps);

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

        const QVariantMap ruleGaps = currentContextGapOverride(layoutRegistry, settings, screenId);
        int zonePadding = getEffectiveInnerGap(layout, settings, ruleGaps);
        ::PhosphorLayout::EdgeGaps outerGaps = getEffectiveOuterGaps(layout, settings, ruleGaps);

        return buildEmptyZoneListImpl(mgr, layout, std::optional<QRect>(vsGeom), vsAvailGeom, screenId, zonePadding,
                                      outerGaps, useAvail, settings, vsGeom, nullptr, isZoneEmpty);
    }

    return physScreen ? buildEmptyZoneList(mgr, layout, physScreen, settings, isZoneEmpty, layoutRegistry)
                      : PhosphorProtocol::EmptyZoneList{};
}

} // namespace GeometryUtils

} // namespace PlasmaZones
