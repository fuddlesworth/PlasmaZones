// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Auto-snap calculation methods (moved from WindowTrackingService).
// Part of SnapEngine — split into its own translation unit for SRP.

#include <PhosphorSnapEngine/SnapEngine.h>
#include <PhosphorZones/AssignmentEntry.h>
#include <PhosphorSnapEngine/SnapState.h>
#include <PhosphorZones/Layout.h>
#include <PhosphorZones/Zone.h>
#include <PhosphorZones/LayoutRegistry.h>
#include <PhosphorScreens/Manager.h>
#include <PhosphorScreens/ScreenIdentity.h>
#include <PhosphorScreens/VirtualScreen.h>
#include <PhosphorSnapEngine/ISnapSettings.h>
#include "snapenginelogging.h"
#include <QGuiApplication>
#include <QScreen>
#include <QUuid>

namespace PhosphorSnapEngine {

using PhosphorEngine::PendingRestore;
using PhosphorEngine::SnapResult;
using PhosphorEngine::StickyWindowHandling;

// ═══════════════════════════════════════════════════════════════════════════════
// Auto-Snap Logic
// ═══════════════════════════════════════════════════════════════════════════════

SnapResult SnapEngine::calculateSnapToPlacementRule(const QString& windowId, const QString& windowScreenName,
                                                    bool isSticky) const
{
    // NOTE: deliberately NO isWindowFloating() guard here. A SnapToZone rule is an
    // explicit "this app belongs in these zones" directive that outranks float
    // state — both on open (it overrides a remembered floated position) and on a
    // Meta+F un-float (the rule is the authoritative un-float target). The old
    // per-layout app-rule path skipped floating windows; the rule-driven path
    // intentionally does not.

    // Check sticky window handling
    if (auto* s = snapSettings(); isSticky && s) {
        auto handling = s->snappingStickyWindowHandling();
        if (handling == PhosphorEngine::StickyWindowHandling::IgnoreAll) {
            return SnapResult::noSnap();
        }
    }

    if (!m_layoutManager) {
        return SnapResult::noSnap();
    }

    // The placement resolver is the daemon's SnapToZone window-rule evaluation —
    // the engine never reads the rule store directly (LGPL boundary). It returns
    // the 1-based zone ordinals to snap into, or an empty list when no SnapToZone
    // rule matched this window. Unset resolver (unit tests) ⇒ no rule snapping.
    if (!m_placementZonesResolver) {
        return SnapResult::noSnap();
    }
    const QList<int> ordinals = m_placementZonesResolver(windowId);
    if (ordinals.isEmpty()) {
        return SnapResult::noSnap();
    }

    // Ordinals are layout-agnostic: resolve them against whatever layout is active
    // on the window's CURRENT screen. Legacy per-layout app rules could target a
    // different screen; that is now expressed as a ScreenId match on the rule
    // itself (set during v3→v4 migration), so there is no cross-screen scan here.
    PhosphorZones::Layout* layout = m_layoutManager->resolveLayoutForScreen(windowScreenName);
    if (!layout) {
        qCDebug(PhosphorSnapEngine::lcSnapEngine)
            << "calculateSnapToPlacementRule: no layout for screen" << windowScreenName;
        return SnapResult::noSnap();
    }

    // Resolve each ordinal to its zone geometry and union them (zone span). An
    // ordinal naming a zone the active layout lacks is skipped — a span rule is
    // layout-agnostic and may reference a zone count this layout does not have.
    QStringList zoneIds;
    QRect unionGeo;
    for (const int ordinal : ordinals) {
        PhosphorZones::Zone* zone = layout->zoneByNumber(ordinal);
        if (!zone) {
            qCDebug(PhosphorSnapEngine::lcSnapEngine)
                << "calculateSnapToPlacementRule: zone ordinal" << ordinal << "absent in layout" << layout->name();
            continue;
        }
        const QString zoneId = zone->id().toString();
        const QRect geo = m_windowTracker->zoneGeometry(zoneId, windowScreenName);
        if (!geo.isValid()) {
            continue;
        }
        zoneIds.append(zoneId);
        // Default QRect is empty, so the first united() returns geo unchanged.
        unionGeo = unionGeo.united(geo);
    }

    if (zoneIds.isEmpty() || !unionGeo.isValid()) {
        return SnapResult::noSnap();
    }

    qCInfo(PhosphorSnapEngine::lcSnapEngine) << "calculateSnapToPlacementRule: snapping" << windowId << "to zones"
                                             << ordinals << "on screen" << windowScreenName;

    SnapResult result;
    result.shouldSnap = true;
    result.geometry = unionGeo;
    result.zoneId = zoneIds.first();
    result.zoneIds = zoneIds;
    result.screenId = windowScreenName;
    return result;
}

SnapResult SnapEngine::calculateSnapToLastZone(const QString& windowId, const QString& windowScreenId,
                                               bool isSticky) const
{
    // Check if feature is enabled
    auto* s = snapSettings();
    if (!s || !s->moveNewWindowsToLastZone()) {
        return SnapResult::noSnap();
    }

    // Check if window was floating - floating windows should NOT be auto-snapped
    // They should remain floating when reopened
    if (m_windowTracker->isWindowFloating(windowId)) {
        qCDebug(PhosphorSnapEngine::lcSnapEngine) << "snapToLastZone:" << windowId << "was floating, skipping";
        return SnapResult::noSnap();
    }

    // Check sticky window handling
    if (isSticky) {
        auto handling = s->snappingStickyWindowHandling();
        if (handling == PhosphorEngine::StickyWindowHandling::IgnoreAll
            || handling == PhosphorEngine::StickyWindowHandling::RestoreOnly) {
            return SnapResult::noSnap();
        }
    }

    // Need a last used zone
    const QString lastUsedZoneId = m_snapState->lastUsedZoneId();
    if (lastUsedZoneId.isEmpty()) {
        return SnapResult::noSnap();
    }

    // Check if window class was ever user-snapped
    QString windowClass = m_windowTracker->currentAppIdFor(windowId);
    if (!m_snapState->userSnappedClasses().contains(windowClass)) {
        return SnapResult::noSnap();
    }

    // Validate virtual screen still exists — configuration may have changed since last snap.
    // Fall back to physical screen ID if the virtual screen was removed.
    const QString lastUsedScreenId = m_snapState->lastUsedScreenId();
    QString effectiveScreenId = m_windowTracker->resolveEffectiveScreenId(lastUsedScreenId);

    // Don't cross-screen snap
    if (!windowScreenId.isEmpty() && !effectiveScreenId.isEmpty()
        && !PhosphorScreens::ScreenIdentity::screensMatch(windowScreenId, effectiveScreenId)) {
        return SnapResult::noSnap();
    }

    // Check virtual desktop match (unless sticky or desktop 0 = all)
    const int lastUsedDesktop = m_snapState->lastUsedDesktop();
    if (!isSticky && m_virtualDesktopManager && lastUsedDesktop > 0) {
        int currentDesktop = m_virtualDesktopManager->currentDesktop();
        if (currentDesktop != lastUsedDesktop) {
            return SnapResult::noSnap();
        }
    }

    // Calculate geometry
    QRect geo = m_windowTracker->zoneGeometry(lastUsedZoneId, effectiveScreenId);
    if (!geo.isValid()) {
        return SnapResult::noSnap();
    }

    SnapResult result;
    result.shouldSnap = true;
    result.geometry = geo;
    result.zoneId = lastUsedZoneId;
    result.zoneIds = QStringList{lastUsedZoneId};
    result.screenId = effectiveScreenId;
    return result;
}

SnapResult SnapEngine::calculateSnapToEmptyZone(const QString& windowId, const QString& windowScreenId,
                                                bool isSticky) const
{
    // Do NOT skip floating windows here: this is called when the user explicitly dropped
    // a window on a monitor (dragStopped, no zone snap). If that monitor has auto-assign,
    // filling the first empty zone is intended. Floating list is for restore/last-zone
    // auto-snap; we clear floating state when we assign in snapToEmptyZone.

    // Check sticky window handling (auto-assign is an auto-snap, not a restore)
    if (auto* s = snapSettings(); isSticky && s) {
        auto handling = s->snappingStickyWindowHandling();
        if (handling == PhosphorEngine::StickyWindowHandling::IgnoreAll
            || handling == PhosphorEngine::StickyWindowHandling::RestoreOnly) {
            qCDebug(PhosphorSnapEngine::lcSnapEngine)
                << "snapToEmptyZone: window" << m_windowTracker->currentAppIdFor(windowId) << "sticky handling"
                << static_cast<int>(handling);
            return SnapResult::noSnap();
        }
    }

    // Check the effective auto-assign gate. Autotile screens are filtered out
    // upstream (lifecycle.cpp short-circuits before reaching here), so by the
    // time we evaluate this gate we're always operating on a manual layout.
    // The global "all layouts" master toggle (#370) is a force-on override
    // that lights up auto-assign for any such layout regardless of its
    // per-layout flag; when the global toggle is off, the per-layout flag
    // remains the sole gate.
    PhosphorZones::Layout* layout = m_layoutManager->resolveLayoutForScreen(windowScreenId);
    if (!layout) {
        qCDebug(PhosphorSnapEngine::lcSnapEngine) << "snapToEmptyZone: no layout for screen" << windowScreenId;
        return SnapResult::noSnap();
    }
    auto* settings = snapSettings();
    const bool effectiveAuto = layout->autoAssign() || (settings && settings->autoAssignAllLayouts());
    if (!effectiveAuto) {
        qCDebug(PhosphorSnapEngine::lcSnapEngine) << "snapToEmptyZone: layout" << layout->name() << "autoAssign=false";
        return SnapResult::noSnap();
    }

    // Reuse findEmptyZoneInLayout() with already-resolved layout to avoid double resolution.
    // Filter occupancy by the current virtual desktop so windows parked on other desktops
    // don't prevent auto-snap placement on the current desktop.
    const int desktopFilter = m_virtualDesktopManager ? m_virtualDesktopManager->currentDesktop() : 0;
    QString emptyZoneId = m_windowTracker->findEmptyZoneInLayout(layout, windowScreenId, desktopFilter);
    if (emptyZoneId.isEmpty()) {
        qCDebug(PhosphorSnapEngine::lcSnapEngine) << "snapToEmptyZone: no empty zone on" << windowScreenId;
        return SnapResult::noSnap();
    }

    QRect geo = m_windowTracker->zoneGeometry(emptyZoneId, windowScreenId);
    if (!geo.isValid()) {
        qCDebug(PhosphorSnapEngine::lcSnapEngine) << "snapToEmptyZone: invalid geometry for zone" << emptyZoneId;
        return SnapResult::noSnap();
    }

    return {true, geo, emptyZoneId, {emptyZoneId}, windowScreenId};
}

} // namespace PhosphorSnapEngine
