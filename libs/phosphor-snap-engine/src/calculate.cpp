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
    const PlacementDirective directive = m_placementZonesResolver(windowId, windowScreenName);
    if (directive.zoneOrdinals.isEmpty()) {
        return SnapResult::noSnap();
    }
    const QList<int>& ordinals = directive.zoneOrdinals;

    // A RouteToScreen action pins the placement to a specific monitor: resolve the
    // zones on THAT screen and move the window there (the apply path honours
    // result.screenId — commitSnap, the disabled-context gate, and the returned
    // geometry all key off it, so cross-screen migration reuses the same machinery
    // a cross-screen snapped-record restore uses). Empty target ⇒ the window's
    // opening screen, the historical behaviour (a ScreenId match leaf only SCOPES
    // such a rule; RouteToScreen is what ROUTES it).
    const QString placementScreen = directive.targetScreenId.isEmpty() ? windowScreenName : directive.targetScreenId;

    // A RouteToDesktop action snaps the window into its zone on the DESTINATION
    // desktop's layout, not the one it momentarily opened on: resolve the layout and
    // gate the mode in that desktop's context, and stamp the result so the commit
    // records the assignment there too. 0 ⇒ no desktop routing ⇒ the placement
    // screen's current desktop (the historical behaviour).
    const int placementDesktop =
        directive.targetDesktop >= 1 ? directive.targetDesktop : currentVirtualDesktopForScreen(placementScreen);

    // The placement only applies when the (screen, desktop) target is in snapping
    // mode. An autotile-mode target is owned by the autotile routing hook, and a
    // disabled / unresolvable target has no layout — decline so the window falls
    // through to the normal restore chain rather than being stranded. Gate whenever
    // the target differs from the opening (screen, desktop), so a cross-desktop or
    // cross-screen route is validated against where the window will actually land.
    // (m_layoutManager is non-null here — the early guard above already returned.)
    const bool routed = placementScreen != windowScreenName || directive.targetDesktop >= 1;
    if (routed
        && m_layoutManager->modeForScreen(placementScreen, placementDesktop, currentActivity())
            != PhosphorZones::AssignmentEntry::Mode::Snapping) {
        qCDebug(PhosphorSnapEngine::lcSnapEngine)
            << "calculateSnapToPlacementRule: route target" << placementScreen << "desktop" << placementDesktop
            << "is not in snapping mode — declining snap route for" << windowId;
        return SnapResult::noSnap();
    }

    // Ordinals are layout-agnostic: resolve them against the layout active on the
    // placement (screen, desktop). For a desktop route that is the DESTINATION
    // desktop's layout; otherwise it is the screen's current-desktop layout.
    PhosphorZones::Layout* layout = directive.targetDesktop >= 1
        ? m_layoutManager->layoutForScreen(placementScreen, placementDesktop, currentActivity())
        : m_layoutManager->resolveLayoutForScreen(placementScreen);
    if (!layout) {
        qCDebug(PhosphorSnapEngine::lcSnapEngine)
            << "calculateSnapToPlacementRule: no layout for screen" << placementScreen << "desktop" << placementDesktop;
        return SnapResult::noSnap();
    }

    // Resolve each ordinal to its zone id (an ordinal naming a zone the active
    // layout lacks is skipped — a span rule is layout-agnostic and may reference
    // a zone count this layout does not have).
    QStringList zoneIds;
    for (const int ordinal : ordinals) {
        PhosphorZones::Zone* zone = layout->zoneByNumber(ordinal);
        if (!zone) {
            qCDebug(PhosphorSnapEngine::lcSnapEngine)
                << "calculateSnapToPlacementRule: zone ordinal" << ordinal << "absent in layout" << layout->name();
            continue;
        }
        zoneIds.append(zone->id().toString());
    }
    if (zoneIds.isEmpty()) {
        return SnapResult::noSnap();
    }

    // Union via resolveZoneGeometry so the span uses the SAME QRectF-union-then-
    // align rounding the float-back poison guard uses (WTA::captureWindowPlacement
    // → resolveZoneGeometry). A per-QRect union here would diverge by a pixel at
    // fractional scaling, so a window floated off the span without moving would
    // leak the snap rect into its float-back geometry.
    const QRect unionGeo = m_windowTracker->resolveZoneGeometry(zoneIds, placementScreen);
    if (!unionGeo.isValid()) {
        return SnapResult::noSnap();
    }

    qCInfo(PhosphorSnapEngine::lcSnapEngine)
        << "calculateSnapToPlacementRule: snapping" << windowId << "to zones" << ordinals << "on screen"
        << placementScreen << (directive.targetScreenId.isEmpty() ? "(opening screen)" : "(routed)");

    SnapResult result;
    result.shouldSnap = true;
    result.geometry = unionGeo;
    result.zoneId = zoneIds.first();
    result.zoneIds = zoneIds;
    result.screenId = placementScreen;
    result.virtualDesktop = directive.targetDesktop; // 0 ⇒ commit on the current desktop
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
        int currentDesktop = currentVirtualDesktopForScreen(windowScreenId);
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
    const int desktopFilter = currentVirtualDesktopForScreen(windowScreenId);
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
