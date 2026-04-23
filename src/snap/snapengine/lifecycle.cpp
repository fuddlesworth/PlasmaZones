// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "../SnapEngine.h"
#include <PhosphorZones/SnapState.h>
#include <PhosphorZones/AssignmentEntry.h>
#include "core/interfaces.h"
#include <PhosphorZones/LayoutRegistry.h>
#include "core/logging.h"
#include "core/utils.h"
#include "core/virtualdesktopmanager.h"
#include "core/windowtrackingservice.h"

namespace PlasmaZones {

// ═══════════════════════════════════════════════════════════════════════════════
// windowOpened — delegates to resolveWindowRestore() and applies the result
// ═══════════════════════════════════════════════════════════════════════════════

void SnapEngine::windowOpened(const QString& windowId, const QString& screenId, int minWidth, int minHeight)
{
    Q_UNUSED(minWidth)
    Q_UNUSED(minHeight)

    if (windowId.isEmpty() || screenId.isEmpty()) {
        return;
    }

    // Mark this window as reported by the effect (confirmed live). This lets the
    // sibling check in calculateRestoreFromSession distinguish live windows from
    // stale config entries after KWin restart (where UUIDs changed).
    markWindowReported(windowId);

    // Guard: skip if already snapped (prevents double-assignment when both
    // windowOpened and the WTA D-Bus resolveWindowRestore path run for the
    // same window — e.g., effect calls windowOpened then D-Bus resolveWindowRestore).
    // Also consume the appId-based pending entry so other instances of the same app
    // (with different UUIDs) don't incorrectly steal this window's zone.
    if (m_snapState->isWindowSnapped(windowId)) {
        m_windowTracker->consumePendingAssignment(windowId);
        qCDebug(lcCore) << "SnapEngine::windowOpened: window" << windowId << "already snapped, skipping";
        return;
    }

    SnapResult result = resolveWindowRestore(windowId, screenId, false);
    if (!result.shouldSnap) {
        return;
    }

    // Apply: mark as auto-snapped first so the flag persists through
    // commitSnap (AutoRestored intent leaves it alone). Then commit via
    // the unified orchestration — clear floating, assign to zone(s),
    // emit windowSnapStateChanged / windowFloatingClearedForSnap as
    // appropriate. consumePendingAssignment is NOT called here because
    // resolveWindowRestore already consumed its entry (see
    // calculateRestoreFromSession + the explicit consume on line 147
    // above). Last-used-zone update is skipped by AutoRestored intent.
    m_snapState->markAsAutoSnapped(windowId);
    const QStringList zoneIds = result.zoneIds.isEmpty() ? QStringList{result.zoneId} : result.zoneIds;
    if (zoneIds.size() > 1) {
        commitMultiZoneSnap(windowId, zoneIds, result.screenId, SnapIntent::AutoRestored);
    } else {
        commitSnap(windowId, zoneIds.first(), result.screenId, SnapIntent::AutoRestored);
    }

    // Emit geometry for KWin effect to apply
    Q_EMIT applyGeometryRequested(windowId, result.geometry.x(), result.geometry.y(), result.geometry.width(),
                                  result.geometry.height(), result.zoneId, result.screenId, false);

    qCInfo(lcCore) << "SnapEngine::windowOpened: snapped" << windowId << "to zone" << result.zoneId << "on"
                   << result.screenId;
}

// ═══════════════════════════════════════════════════════════════════════════════
// resolveWindowRestore — 4-level auto-snap fallback chain
//
// Mostly decision logic: returns a SnapResult for the caller to apply geometry.
// Side effects: consumePendingAssignment (step 2), navigationFeedback emit
// (floating windows). The caller (windowOpened or WTA D-Bus facade) handles
// zone assignment and geometry application.
//
// Screen mode semantics:
//   - Levels 1 (app rules) and 2 (persisted session) may cross-screen migrate:
//     an app rule or a saved PendingRestore can route a window from the
//     caller's screen to a completely different screen, and
//     calculateRestoreFromSession refuses to place a window on an
//     autotile-mode saved screen (autotile on that screen will own it).
//   - Levels 3 (empty zone) and 4 (last zone) inherently use the caller
//     screen as the target, so they are ONLY valid when the caller's screen
//     is in snap mode. On autotile screens they're short-circuited — stale
//     snap zones on a now-autotile screen must not bleed into placement.
// ═══════════════════════════════════════════════════════════════════════════════

SnapResult SnapEngine::resolveWindowRestore(const QString& windowId, const QString& screenId, bool sticky)
{
    if (windowId.isEmpty() || screenId.isEmpty()) {
        return SnapResult::noSnap();
    }

    // Pre-check: if this window already has an exact zone assignment (loaded from
    // KConfig with full windowId after daemon-only restart), skip the restore chain.
    // Consume the appId-based pending entry to prevent other instances of the same
    // app from incorrectly stealing this window's zone assignment.
    if (m_snapState->isWindowSnapped(windowId)) {
        m_windowTracker->consumePendingAssignment(windowId);
        qCDebug(lcCore) << "resolveWindowRestore:" << windowId << "already has assignment, skipping";
        return SnapResult::noSnap();
    }

    // Exclusion check: skip auto-snap for excluded applications/window classes.
    // This must run before any calculate method so excluded apps are never snapped
    // by app rules, session restore, empty zone, or last zone features.
    //
    // Use the WTS's registry-aware lookup so a window whose class the effect
    // has already updated (Electron/CEF apps renaming themselves) matches
    // against its CURRENT class, not a stale first-seen one. m_windowTracker
    // is non-null at runtime; production code never reaches this with a null
    // tracker.
    if (m_settings && m_windowTracker) {
        const QString appId = m_windowTracker->currentAppIdFor(windowId);
        for (const QString& excluded : m_settings->excludedApplications()) {
            if (PhosphorIdentity::WindowId::appIdMatches(appId, excluded)) {
                qCInfo(lcCore) << "resolveWindowRestore:" << windowId << "excluded by application rule:" << excluded;
                return SnapResult::noSnap();
            }
        }
        for (const QString& excluded : m_settings->excludedWindowClasses()) {
            if (PhosphorIdentity::WindowId::appIdMatches(appId, excluded)) {
                qCInfo(lcCore) << "resolveWindowRestore:" << windowId << "excluded by window class rule:" << excluded;
                return SnapResult::noSnap();
            }
        }
    }

    // 0. Floating windows should not be auto-snapped — emit OSD feedback
    if (m_snapState->isFloating(windowId)) {
        qCInfo(lcCore) << "resolveWindowRestore: window" << windowId << "is floating, skipping snap";
        Q_EMIT navigationFeedback(true, QStringLiteral("float"), QStringLiteral("floated"), QString(), QString(),
                                  screenId);
        return SnapResult::noSnap();
    }

    // 1. App rules (highest priority). May cross-screen migrate.
    {
        SnapResult result = calculateSnapToAppRule(windowId, screenId, sticky);
        if (result.shouldSnap) {
            qCInfo(lcCore) << "resolveWindowRestore: appRule matched for" << windowId << "zone=" << result.zoneId;
            return result;
        }
    }

    // 2. Persisted zone (session restore). May cross-screen migrate: the
    // PendingRestore entry records the saved screen, and
    // calculateRestoreFromSession returns noSnap if that screen is now in
    // autotile mode (letting the autotile engine own it).
    if (m_settings && m_settings->restoreWindowsToZonesOnLogin()) {
        SnapResult result = calculateRestoreFromSession(windowId, screenId, sticky);
        if (result.shouldSnap) {
            m_windowTracker->consumePendingAssignment(windowId);
            qCInfo(lcCore) << "resolveWindowRestore: persisted matched for" << windowId << "zone=" << result.zoneId
                           << "screen=" << result.screenId;
            return result;
        }
    }

    // Levels 3 and 4 inherently target the caller's screen (the empty-zone /
    // last-zone lookups are scoped to screenId, not to a saved zone). If the
    // caller's screen is now in autotile mode, skip them — stale snap zones
    // on an autotile screen must not be auto-assigned, autotile owns
    // placement there.
    if (m_layoutManager) {
        int dt = m_virtualDesktopManager ? m_virtualDesktopManager->currentDesktop() : 0;
        if (m_layoutManager->modeForScreen(screenId, dt, m_layoutManager->currentActivity())
            != PhosphorZones::AssignmentEntry::Mode::Snapping) {
            qCDebug(lcCore) << "resolveWindowRestore:" << windowId << "caller screen" << screenId
                            << "is autotile — skipping empty/last zone fallbacks";
            return SnapResult::noSnap();
        }
    }

    // 3. Auto-assign to empty zone
    {
        SnapResult result = calculateSnapToEmptyZone(windowId, screenId, sticky);
        if (result.shouldSnap) {
            qCInfo(lcCore) << "resolveWindowRestore: emptyZone matched for" << windowId << "zone=" << result.zoneId;
            return result;
        }
    }

    // 4. Snap to last zone (final fallback)
    {
        SnapResult result = calculateSnapToLastZone(windowId, screenId, sticky);
        if (result.shouldSnap) {
            qCInfo(lcCore) << "resolveWindowRestore: lastZone matched for" << windowId << "zone=" << result.zoneId;
            return result;
        }
    }

    return SnapResult::noSnap();
}

} // namespace PlasmaZones
