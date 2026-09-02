// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorPlacement/WindowTrackingService.h>
#include "placementutils.h"

#include <PhosphorEngine/IGeometrySettings.h>
#include <PhosphorGeometry/GeometryUtils.h>
#include <PhosphorLayoutApi/EdgeGaps.h>
#include <PhosphorZones/GeometryUtils.h>
#include <PhosphorZones/Layout.h>
#include <PhosphorZones/LayoutUtils.h>
#include <PhosphorSnapEngine/SnapState.h>
#include <PhosphorScreens/Manager.h>
#include <PhosphorZones/Zone.h>
#include <PhosphorZones/LayoutRegistry.h>
#include <PhosphorWorkspaces/VirtualDesktopManager.h>
#include <PhosphorIdentity/WindowId.h>
#include "placementlogging.h"
#include <QGuiApplication>
#include <QScreen>
#include <QSet>
#include <QUuid>
#include <algorithm>
#include <PhosphorScreens/ScreenIdentity.h>

namespace PhosphorPlacement {

// Snapped-window frames are passed through the shared
// PhosphorGeometry::insetRect helper (the autotile path is deliberately
// un-inset and does not call it) with the
// inset from IGeometryResolver::snapBorderInset, which returns 0 in all
// configurations today: the KWin effect's border shader recolours the window's
// own outermost band INSIDE the frame, so frames are not inset and a snapped
// window fills its zone exactly (no border-width gap between tiles). insetRect
// is retained as the seam for a future per-window-border design that would draw
// outside the frame; with inset 0 it is a no-op.

// ═══════════════════════════════════════════════════════════════════════════════
// Navigation Helpers
// ═══════════════════════════════════════════════════════════════════════════════

QSet<QUuid> WindowTrackingService::buildOccupiedZoneSet(const QString& screenFilter, int desktopFilter) const
{
    QSet<QUuid> occupiedZoneIds;
    forEachZoneAssignedWindow(
        [&](const QString& windowId, const QStringList& zoneIds, const QString& windowScreen, int windowDesktop) {
            // Skip floating windows — they have preserved zone assignments (for resnap
            // on mode switch) but should not make zones appear occupied.
            if (isWindowFloating(windowId)) {
                return;
            }
            // When screen filter is set, only count zones from windows on that screen.
            // This prevents windows on other screens (or desktops sharing the same layout)
            // from making zones appear occupied on the target screen.
            if (!screenFilter.isEmpty() && !PhosphorScreens::ScreenIdentity::screensMatch(windowScreen, screenFilter)) {
                return;
            }
            // When desktop filter is set, only count zones from windows on that desktop.
            // Desktop 0 means "all desktops" (pinned window) — always include those.
            if (!desktopMatchesFilter(windowDesktop, desktopFilter)) {
                return;
            }
            for (const QString& zoneId : zoneIds) {
                if (zoneId.startsWith(kZoneSelectorIdPrefix)) {
                    continue;
                }
                auto uuid = parseUuid(zoneId);
                if (uuid) {
                    occupiedZoneIds.insert(*uuid);
                }
            }
        });
    return occupiedZoneIds;
}

namespace {
// Suppress-aware predicate shared by the zone resolvers below (#724 family).
// Takes the desktop EXPLICITLY so every caller judges suppression on the same
// desktop it filters occupancy by — the registry's own
// currentVirtualDesktopForScreen is a second authority that can lag the
// VirtualDesktopManager the callers use.
bool activeLayoutSuppressedFor(const PhosphorZones::LayoutRegistry* registry, const QString& screenId,
                               int virtualDesktop)
{
    if (!registry || screenId.isEmpty()) {
        return false;
    }
    // Desktop 0 means "unknown / no desktop filter" to this function's
    // callers, but isContextActiveLayoutSuppressed would read it as literal
    // desktop 0 — matching no per-desktop assignment and reporting suppressed
    // for a context that is actually covered. Resolve the real desktop
    // instead; only a registry that cannot answer leaves it at 0.
    const int desktop = virtualDesktop > 0 ? virtualDesktop : registry->currentVirtualDesktopForScreen(screenId);
    return registry->isContextActiveLayoutSuppressed(screenId, desktop, registry->currentActivity());
}
} // namespace

QString WindowTrackingService::findEmptyZoneInLayout(PhosphorZones::Layout* layout, const QString& screenId,
                                                     int desktopFilter) const
{
    if (!layout) {
        return QString();
    }
    // Suppress-aware guard lives HERE, in the shared workhorse, because the
    // auto-snap chain (SnapEngine::snapToEmptyZone) and the unfloat fallback
    // tier resolve the layout themselves and call straight into this function
    // — gating only the findEmptyZone/getEmptyZones wrappers would miss them.
    // resolveLayoutForScreen falls back to the GLOBAL default layout for an
    // unassigned screen, so without this a screen whose default assignment is
    // suppressed still yields zones and windows get placed into zones the
    // screen does not have (#724 family).
    if (activeLayoutSuppressedFor(m_layoutManager, screenId, desktopFilter)) {
        return QString();
    }

    QSet<QUuid> occupiedZoneIds = buildOccupiedZoneSet(screenId, desktopFilter);

    // Sort by zone number so "first empty" is the lowest-numbered empty zone
    QVector<PhosphorZones::Zone*> sortedZones = layout->zones();
    PhosphorZones::LayoutUtils::sortZonesByNumber(sortedZones);

    for (PhosphorZones::Zone* zone : sortedZones) {
        if (!occupiedZoneIds.contains(zone->id())) {
            return zone->id().toString();
        }
    }
    return QString();
}

QString WindowTrackingService::findEmptyZone(const QString& screenId) const
{
    PhosphorZones::Layout* layout = m_layoutManager->resolveLayoutForScreen(screenId);
    const int desktopFilter = m_virtualDesktopManager ? m_virtualDesktopManager->currentDesktopForScreen(screenId) : 0;
    return findEmptyZoneInLayout(layout, screenId, desktopFilter);
}

PhosphorProtocol::EmptyZoneList WindowTrackingService::getEmptyZones(const QString& screenId) const
{
    // Same guard as findEmptyZoneInLayout (this function builds its own list
    // rather than routing through it), on the same desktop authority the
    // occupancy filter below uses.
    const int desktopFilter = m_virtualDesktopManager ? m_virtualDesktopManager->currentDesktopForScreen(screenId) : 0;
    if (activeLayoutSuppressedFor(m_layoutManager, screenId, desktopFilter)) {
        return {};
    }
    PhosphorZones::Layout* layout = m_layoutManager->resolveLayoutForScreen(screenId);
    if (!layout) {
        return {};
    }

    // Resolve physical screen for fallback (virtual screen IDs resolve to their backing physical output)
    const PhosphorScreens::PhysicalScreen screen =
        m_screenManager ? m_screenManager->physicalScreenFor(screenId) : PhosphorScreens::PhysicalScreen{};
    QRect physicalGeom = screen.geometry;
    if (!m_screenManager) {
        if (QScreen* primary = QGuiApplication::primaryScreen()) {
            physicalGeom = primary->geometry();
        }
    }
    if (!physicalGeom.isValid()) {
        return {};
    }

    // Resolve the SCOPE the layout was authored for. For a virtual
    // screen the scope is the VS sub-rect within the physical monitor;
    // for a non-VS screenId it's the physical screen's geometry. The
    // overlay surface that renders snap-assist is anchored to the VS
    // top-left, so emitting zone coordinates relative to anything
    // other than the VS rect produces zone rectangles wider than the
    // surface, manifesting as "zone right edge clipped flat without
    // a rounded corner" because the visible content can't extend past
    // the surface's bounds.
    //
    // Pre-fix path: `getZoneGeometryWithGaps(mgr, zone, screen, …)` +
    // `availableAreaToOverlayCoordinates(geom, screen->geometry())`
    // computed against the PHYSICAL screen, so a layout assigned to
    // a smaller VS produced zones sized for the full monitor.
    QRect scopeGeom = m_screenManager ? m_screenManager->screenGeometry(screenId) : QRect();
    if (!scopeGeom.isValid()) {
        scopeGeom = physicalGeom;
    }
    const QRect scopeAvailGeom = m_screenManager ? m_screenManager->actualAvailableGeometry(screen) : scopeGeom;
    const QRect availForLayout =
        scopeAvailGeom.intersected(scopeGeom).isEmpty() ? scopeGeom : scopeAvailGeom.intersected(scopeGeom);

    // No `LayoutComputeService::recalculateSync` here. That call mutates
    // the shared layout's cached zone geometries to whatever rect we
    // pass, which clobbers OSD / main-overlay consumers reading
    // `zone->geometry()` against the last compute pass. The VS-aware
    // `getZoneGeometryWithGaps(mgr, zone, scopeGeom, availForLayout, …)`
    // overload below computes from `zone->relativeGeometry()` against
    // the explicit `scopeGeom` rect, so it doesn't need the cache primed.

    // Screen-filtered + desktop-filtered occupancy — without the screen filter,
    // zones occupied on screen A appear occupied on screen B when both use the
    // same layout (same zone IDs). Without the desktop filter, windows parked on
    // other virtual desktops keep their zone occupied on the current desktop,
    // blocking snap assist (discussion #323).
    QSet<QUuid> occupied = buildOccupiedZoneSet(screenId, desktopFilter);
    int zp = m_geometryResolver ? m_geometryResolver->resolveInnerGap(layout, screenId)
                                : PhosphorEngine::GeometryDefaults::InnerGap;
    auto og = m_geometryResolver ? m_geometryResolver->resolveOuterGaps(layout, screenId)
                                 : PhosphorLayout::EdgeGaps::uniform(PhosphorEngine::GeometryDefaults::OuterGap);
    // The null-resolver fallbacks mirror PhosphorZones::ZoneDefaults
    // (BorderWidth=2, BorderRadius=8) — the same defaults DaemonGeometryResolver
    // falls back to when its settings are absent. They are duplicated as
    // literals here because phosphor-placement does not depend on
    // phosphor-zones' defaults header and pulling it in for two constants is
    // disproportionate; this branch is a degenerate safety path (production
    // always wires a DaemonGeometryResolver).
    constexpr int kFallbackBorderWidth = 2;
    constexpr int kFallbackBorderRadius = 8;
    int defaultBw = m_geometryResolver ? m_geometryResolver->defaultBorderWidth() : kFallbackBorderWidth;
    int defaultBr = m_geometryResolver ? m_geometryResolver->defaultBorderRadius() : kFallbackBorderRadius;

    PhosphorProtocol::EmptyZoneList result;
    for (PhosphorZones::Zone* zone : layout->zones()) {
        if (occupied.contains(zone->id())) {
            continue;
        }
        // VS-aware overload: uses the explicit scopeGeom rect for
        // layout maths instead of pulling QScreen::geometry() (which
        // is always physical).
        QRectF geom = PhosphorZones::GeometryUtils::getZoneGeometryWithGaps(
            m_screenManager, zone, scopeGeom, availForLayout, zp, og, !layout->useFullScreenGeometry(), screenId);
        // Translate to overlay-local coords against the SCOPE (VS or
        // physical) origin so zone.x = 0 lines up with the snap-assist
        // surface's top-left anchor.
        QRect overlayGeom =
            PhosphorGeometry::snapToRect(PhosphorGeometry::availableAreaToOverlayCoordinates(geom, scopeGeom));

        PhosphorProtocol::EmptyZoneEntry entry;
        entry.zoneId = zone->id().toString();
        entry.x = overlayGeom.x();
        entry.y = overlayGeom.y();
        entry.width = overlayGeom.width();
        entry.height = overlayGeom.height();
        entry.borderWidth = zone->useCustomColors() ? zone->borderWidth() : defaultBw;
        entry.borderRadius = zone->useCustomColors() ? zone->borderRadius() : defaultBr;
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

namespace {
// Does this request name an output a zone FRAME may be measured against?
//
// With a screen manager present an empty screenId answers false, never the
// primary output. ScreenManager::physicalScreenFor() deliberately maps an empty
// id onto the primary screen (the historical findByIdOrName contract its other
// callers depend on), which is exactly the wrong answer here: every caller of
// zoneGeometry/multiZoneGeometry computes a window frame and only checks
// isValid(), so a primary-screen stand-in does not degrade gracefully. It
// recomputes the zone against the wrong monitor and the window is moved onto
// that monitor, and on same-sized outputs the two rects differ only by the
// output origin so nothing downstream can tell them apart. SnapEngine's
// calculateSnapToLastZone states this contract outright for the disk-restore
// case, which lands an empty lastUsedScreenId and is supposed to end in noSnap.
// Callers that genuinely mean the primary screen resolve it and pass its id
// (WindowTrackingAdaptor::getZoneGeometry does exactly that).
//
// With no screen manager at all there is nothing to resolve against, so the
// primary screen stands in as before.
bool frameScreenIdResolves(const PhosphorScreens::ScreenManager* screenManager, const QString& screenId)
{
    if (!screenManager) {
        return true;
    }
    return !screenId.isEmpty() && screenManager->physicalScreenFor(screenId).isValid();
}

// The QScreen behind a resolved id, when there is one. A tracked output can
// legitimately have none (a synthetic provider, which is what a headless test
// stages), and the geometry helpers below take the manager's own rects for that
// screen id in preference to the QScreen anyway, so a null here is not by itself
// a refusal — frameScreenIdResolves is.
QScreen* frameQScreenFor(const PhosphorScreens::ScreenManager* screenManager, const QString& screenId)
{
    return screenManager ? screenManager->physicalScreenFor(screenId).qscreen : QGuiApplication::primaryScreen();
}
} // namespace

QRect WindowTrackingService::zoneGeometry(const QString& zoneId, const QString& screenId) const
{
    auto uuidOpt = parseUuid(zoneId);
    if (!uuidOpt) {
        return QRect();
    }

    auto [zone, layout] = findZoneInAllLayouts(*uuidOpt);
    if (!zone) {
        return QRect();
    }

    // Resolve physical screen (virtual IDs resolve to their backing physical
    // output). See frameScreenIdResolves above for why an empty screenId is
    // refused rather than measured against the primary output.
    if (!frameScreenIdResolves(m_screenManager, screenId)) {
        return QRect();
    }
    QScreen* screen = frameQScreenFor(m_screenManager, screenId);
    if (!screen && !m_screenManager) {
        return QRect();
    }

    int zp = m_geometryResolver ? m_geometryResolver->resolveInnerGap(layout, screenId)
                                : PhosphorEngine::GeometryDefaults::InnerGap;
    auto og = m_geometryResolver ? m_geometryResolver->resolveOuterGaps(layout, screenId)
                                 : PhosphorLayout::EdgeGaps::uniform(PhosphorEngine::GeometryDefaults::OuterGap);
    QRect geo =
        PhosphorZones::GeometryUtils::getZoneGeometryForScreen(m_screenManager, zone, screen, screenId, layout, zp, og);
    // Reserved snap-border inset seam: snapBorderInset() returns 0 in every
    // config today (the border shader recolours the window's own band, no
    // geometry inset), so this is currently a no-op. Single chokepoint for
    // actual window frames — snap-assist previews use getZoneGeometryWithGaps
    // directly (buildEmptyZoneList) and bypass this, so previews stay un-inset.
    int inset = m_geometryResolver ? m_geometryResolver->snapBorderInset() : 0;
    return PhosphorGeometry::insetRect(geo, inset);
}

QRect WindowTrackingService::multiZoneGeometry(const QStringList& zoneIds, const QString& screenId) const
{
    // Unite zone geometries as QRectF first, then round once at the end.
    // Uniting independently-rounded QRects can produce 1px gaps at fractional
    // scaling factors (e.g. 1.2x on ultrawides).
    QRectF combined;
    // Same resolution rule as zoneGeometry() above: with a screen manager present,
    // an empty or unresolvable screenId answers invalid rather than falling back
    // to the primary output.
    if (!frameScreenIdResolves(m_screenManager, screenId)) {
        return combined.toAlignedRect();
    }
    QScreen* screen = frameQScreenFor(m_screenManager, screenId);
    if (!screen && !m_screenManager) {
        return combined.toAlignedRect();
    }
    for (const QString& zoneId : zoneIds) {
        auto uuidOpt = parseUuid(zoneId);
        if (!uuidOpt) {
            continue;
        }

        auto [zone, layout] = findZoneInAllLayouts(*uuidOpt);
        if (!zone) {
            continue;
        }

        QRectF geoF = PhosphorZones::GeometryUtils::getZoneGeometryForScreenF(
            m_screenManager, zone, screen, screenId, layout,
            m_geometryResolver ? m_geometryResolver->resolveInnerGap(layout, screenId)
                               : PhosphorEngine::GeometryDefaults::InnerGap,
            m_geometryResolver ? m_geometryResolver->resolveOuterGaps(layout, screenId)
                               : PhosphorLayout::EdgeGaps::uniform(PhosphorEngine::GeometryDefaults::OuterGap));
        if (geoF.isValid()) {
            if (combined.isValid()) {
                combined = combined.united(geoF);
            } else {
                combined = geoF;
            }
        }
    }
    // Inset the COMBINED span once (not per sub-zone) so the border traces the
    // outer edge of the multi-zone frame, matching the single per-mode snap
    // border the effect draws. snapBorderInset() returns 0 in every config
    // today (reserved seam), so this is currently a no-op.
    int inset = m_geometryResolver ? m_geometryResolver->snapBorderInset() : 0;
    return PhosphorGeometry::insetRect(combined.toAlignedRect(), inset);
}

} // namespace PhosphorPlacement
