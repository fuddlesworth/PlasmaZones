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
#include <QScreen>
#include <QSet>
#include <QUuid>
#include <algorithm>
#include <PhosphorScreens/ScreenIdentity.h>

namespace PhosphorPlacement {

// ═══════════════════════════════════════════════════════════════════════════════
// Navigation Helpers
// ═══════════════════════════════════════════════════════════════════════════════

QSet<QUuid> WindowTrackingService::buildOccupiedZoneSet(const QString& screenFilter, int desktopFilter) const
{
    const QHash<QString, QStringList>& zones = m_snapState->zoneAssignments();
    const QHash<QString, QString>& screens = m_snapState->screenAssignments();
    const QHash<QString, int>& desktops = m_snapState->desktopAssignments();

    QSet<QUuid> occupiedZoneIds;
    for (auto it = zones.constBegin(); it != zones.constEnd(); ++it) {
        // Skip floating windows — they have preserved zone assignments (for resnap
        // on mode switch) but should not make zones appear occupied.
        if (isWindowFloating(it.key())) {
            continue;
        }
        // When screen filter is set, only count zones from windows on that screen.
        // This prevents windows on other screens (or desktops sharing the same layout)
        // from making zones appear occupied on the target screen.
        if (!screenFilter.isEmpty()) {
            QString windowScreen = screens.value(it.key());
            if (!PhosphorScreens::ScreenIdentity::screensMatch(windowScreen, screenFilter)) {
                continue;
            }
        }
        // When desktop filter is set, only count zones from windows on that desktop.
        // Desktop 0 means "all desktops" (pinned window) — always include those.
        if (desktopFilter > 0) {
            int windowDesktop = desktops.value(it.key(), 0);
            if (windowDesktop != 0 && windowDesktop != desktopFilter) {
                continue;
            }
        }
        for (const QString& zoneId : it.value()) {
            if (zoneId.startsWith(kZoneSelectorIdPrefix)) {
                continue;
            }
            auto uuid = parseUuid(zoneId);
            if (uuid) {
                occupiedZoneIds.insert(*uuid);
            }
        }
    }
    return occupiedZoneIds;
}

QString WindowTrackingService::findEmptyZoneInLayout(PhosphorZones::Layout* layout, const QString& screenId,
                                                     int desktopFilter) const
{
    if (!layout) {
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
    const int desktopFilter = m_virtualDesktopManager ? m_virtualDesktopManager->currentDesktopForScreen(screenId) : 0;
    QSet<QUuid> occupied = buildOccupiedZoneSet(screenId, desktopFilter);
    int zp = m_geometryResolver ? m_geometryResolver->resolveInnerGap(layout, screenId)
                                : PhosphorEngine::GeometryDefaults::InnerGap;
    auto og = m_geometryResolver ? m_geometryResolver->resolveOuterGaps(layout, screenId)
                                 : PhosphorLayout::EdgeGaps::uniform(PhosphorEngine::GeometryDefaults::OuterGap);
    int defaultBw = m_geometryResolver ? m_geometryResolver->defaultBorderWidth() : 2;
    int defaultBr = m_geometryResolver ? m_geometryResolver->defaultBorderRadius() : 0;

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

    // Resolve physical screen (virtual IDs resolve to their backing physical output)
    QScreen* screen =
        m_screenManager ? m_screenManager->physicalScreenFor(screenId).qscreen : QGuiApplication::primaryScreen();
    if (!screen) {
        return QRect();
    }

    int zp = m_geometryResolver ? m_geometryResolver->resolveInnerGap(layout, screenId)
                                : PhosphorEngine::GeometryDefaults::InnerGap;
    auto og = m_geometryResolver ? m_geometryResolver->resolveOuterGaps(layout, screenId)
                                 : PhosphorLayout::EdgeGaps::uniform(PhosphorEngine::GeometryDefaults::OuterGap);
    return PhosphorZones::GeometryUtils::getZoneGeometryForScreen(m_screenManager, zone, screen, screenId, layout, zp,
                                                                  og);
}

QRect WindowTrackingService::multiZoneGeometry(const QStringList& zoneIds, const QString& screenId) const
{
    // Unite zone geometries as QRectF first, then round once at the end.
    // Uniting independently-rounded QRects can produce 1px gaps at fractional
    // scaling factors (e.g. 1.2x on ultrawides).
    QRectF combined;
    QScreen* screen =
        m_screenManager ? m_screenManager->physicalScreenFor(screenId).qscreen : QGuiApplication::primaryScreen();
    if (!screen) {
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
    return combined.toAlignedRect();
}

} // namespace PhosphorPlacement
