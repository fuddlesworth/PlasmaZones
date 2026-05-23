// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorSnapEngine/SnapEngine.h>
#include <PhosphorSnapEngine/SnapState.h>
#include <PhosphorZones/AssignmentEntry.h>
#include <PhosphorSnapEngine/ISnapSettings.h>
#include <PhosphorIdentity/WindowId.h>
#include <PhosphorZones/LayoutRegistry.h>
#include "snapenginelogging.h"

namespace PhosphorSnapEngine {

using PhosphorEngine::PendingRestore;
using PhosphorEngine::SnapIntent;
using PhosphorEngine::SnapResult;

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
        qCDebug(PhosphorSnapEngine::lcSnapEngine)
            << "SnapEngine::windowOpened: window" << windowId << "already snapped, skipping";
        return;
    }

    // The disabled-context gate lives inside resolveWindowRestore so the
    // direct D-Bus path (SnapAdaptor::resolveWindowRestore, used by the
    // KWin effect's per-window restore call) is covered by the same check
    // — see the predicate gate near the top of resolveWindowRestore below.
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

    qCInfo(PhosphorSnapEngine::lcSnapEngine)
        << "SnapEngine::windowOpened: snapped" << windowId << "to zone" << result.zoneId << "on" << result.screenId;
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

SnapResult SnapEngine::resolveWindowRestore(const QString& windowId, const QString& screenId, bool sticky,
                                            PhosphorEngine::WindowKind kind)
{
    if (windowId.isEmpty() || screenId.isEmpty()) {
        return SnapResult::noSnap();
    }

    // Global snapping kill-switch. When the user turns snapping off entirely,
    // no window may be auto-snapped on open — not via app rules, session
    // restore, empty-zone auto-assign, or last-used-zone. The screen-mode gate
    // below only covers autotile-mode screens; a screen still carrying a
    // Snapping-mode layout assignment would otherwise keep auto-snapping new
    // windows even with snapping globally disabled (discussion #461 item 2).
    if (!isEnabled()) {
        qCDebug(PhosphorSnapEngine::lcSnapEngine)
            << "resolveWindowRestore:" << windowId << "snapping globally disabled, skipping";
        return SnapResult::noSnap();
    }

    // Pre-check: if this window already has an exact zone assignment (loaded from
    // KConfig with full windowId after daemon-only restart), skip the restore chain.
    // Consume the appId-based pending entry to prevent other instances of the same
    // app from incorrectly stealing this window's zone assignment.
    if (m_snapState->isWindowSnapped(windowId)) {
        m_windowTracker->consumePendingAssignment(windowId);
        qCDebug(PhosphorSnapEngine::lcSnapEngine)
            << "resolveWindowRestore:" << windowId << "already has assignment, skipping";
        return SnapResult::noSnap();
    }

    // Disabled-context gate: refuse auto-snap onto a (screen, virtualDesktop,
    // activity) the user has disabled snap for. Catches BOTH windowOpened
    // and the direct D-Bus resolveWindowRestore path the KWin effect uses,
    // so a PendingRestore authored before the toggle can no longer drag a
    // freshly opened window into a zone the user told us to stay out of.
    // Without this gate the only fix was a daemon restart — the
    // `isPersistedContextDisabled` filter on disk load fired only once per
    // session, leaving any in-memory entry recorded earlier free to leak
    // through. Discussion #461 item 7.
    //
    // Placed AFTER the isWindowSnapped/consume guard so windows that are
    // already snapped still consume their appId pending entry; placed
    // BEFORE the exclusion lookup and the calculate* chain so app rules,
    // session restore, empty-zone, and last-zone fallbacks are all gated
    // by the same predicate.
    //
    // Predicate is daemon-injected; absence means "no gating" — the
    // historical default that unit tests rely on.
    if (m_shouldRestorePredicate && !m_shouldRestorePredicate(screenId)) {
        qCDebug(PhosphorSnapEngine::lcSnapEngine) << "resolveWindowRestore: skipping" << windowId << "on" << screenId
                                                  << "— disabled-context gate rejected restore";
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
    auto* s = snapSettings();
    if (s && m_windowTracker) {
        const QString appId = m_windowTracker->currentAppIdFor(windowId);
        for (const QString& excluded : s->excludedApplications()) {
            if (PhosphorIdentity::WindowId::appIdMatches(appId, excluded)) {
                qCInfo(PhosphorSnapEngine::lcSnapEngine)
                    << "resolveWindowRestore:" << windowId << "excluded by application rule:" << excluded;
                return SnapResult::noSnap();
            }
        }
        for (const QString& excluded : s->excludedWindowClasses()) {
            if (PhosphorIdentity::WindowId::appIdMatches(appId, excluded)) {
                qCInfo(PhosphorSnapEngine::lcSnapEngine)
                    << "resolveWindowRestore:" << windowId << "excluded by window class rule:" << excluded;
                return SnapResult::noSnap();
            }
        }
    }

    // 0. Floating windows should not be auto-snapped — emit OSD feedback
    if (m_snapState->isFloating(windowId)) {
        qCInfo(PhosphorSnapEngine::lcSnapEngine)
            << "resolveWindowRestore: window" << windowId << "is floating, skipping snap";
        Q_EMIT navigationFeedback(true, QStringLiteral("float"), QStringLiteral("floated"), QString(), QString(),
                                  screenId);
        return SnapResult::noSnap();
    }

    // 1. App rules (highest priority). May cross-screen migrate.
    {
        SnapResult result = calculateSnapToAppRule(windowId, screenId, sticky);
        if (result.shouldSnap) {
            // An app rule may target a screen other than the caller's. The
            // disabled-context gate above validated only the caller's screenId,
            // so re-check the predicate against the result's destination
            // screen. An app rule must not route a window onto a context the
            // user disabled.
            if (m_shouldRestorePredicate && !m_shouldRestorePredicate(result.screenId)) {
                qCDebug(PhosphorSnapEngine::lcSnapEngine) << "resolveWindowRestore:" << windowId << "appRule target"
                                                          << result.screenId << "rejected by disabled-context gate";
                return SnapResult::noSnap();
            }
            qCInfo(PhosphorSnapEngine::lcSnapEngine)
                << "resolveWindowRestore: appRule matched for" << windowId << "zone=" << result.zoneId;
            return result;
        }
    }

    // 2. Persisted zone (session restore). May cross-screen migrate: the
    // PendingRestore entry records the saved screen, and
    // calculateRestoreFromSession returns noSnap if that screen is now in
    // autotile mode (letting the autotile engine own it).
    if (s && s->restoreWindowsToZonesOnLogin()) {
        SnapResult result = calculateRestoreFromSession(windowId, screenId, sticky, kind);
        if (result.shouldSnap) {
            // Session restore may cross-screen migrate: the PendingRestore
            // records its own saved screen. Re-check the predicate against the
            // destination before consuming the pending entry. A restore onto a
            // disabled screen is refused, and the pending entry is left intact
            // so the window can restore once the user re-enables the context.
            if (m_shouldRestorePredicate && !m_shouldRestorePredicate(result.screenId)) {
                qCDebug(PhosphorSnapEngine::lcSnapEngine)
                    << "resolveWindowRestore:" << windowId << "persisted restore target" << result.screenId
                    << "rejected by disabled-context gate";
                return SnapResult::noSnap();
            }
            m_windowTracker->consumePendingAssignment(windowId);
            qCInfo(PhosphorSnapEngine::lcSnapEngine) << "resolveWindowRestore: persisted matched for" << windowId
                                                     << "zone=" << result.zoneId << "screen=" << result.screenId;
            return result;
        }
    }

    // Levels 3 and 4 inherently target the caller's screen (the empty-zone /
    // last-zone lookups are scoped to screenId, not to a saved zone). If the
    // caller's screen is now in autotile mode, skip them — stale snap zones
    // on an autotile screen must not be auto-assigned, autotile owns
    // placement there.
    if (m_layoutManager) {
        const int dt = currentVirtualDesktop();
        if (m_layoutManager->modeForScreen(screenId, dt, currentActivity())
            != PhosphorZones::AssignmentEntry::Mode::Snapping) {
            qCDebug(PhosphorSnapEngine::lcSnapEngine) << "resolveWindowRestore:" << windowId << "caller screen"
                                                      << screenId << "is autotile — skipping empty/last zone fallbacks";
            return SnapResult::noSnap();
        }
    }

    // 3. Auto-assign to empty zone
    {
        SnapResult result = calculateSnapToEmptyZone(windowId, screenId, sticky);
        if (result.shouldSnap) {
            qCInfo(PhosphorSnapEngine::lcSnapEngine)
                << "resolveWindowRestore: emptyZone matched for" << windowId << "zone=" << result.zoneId;
            return result;
        }
    }

    // 4. Snap to last zone (final fallback)
    {
        SnapResult result = calculateSnapToLastZone(windowId, screenId, sticky);
        if (result.shouldSnap) {
            qCInfo(PhosphorSnapEngine::lcSnapEngine)
                << "resolveWindowRestore: lastZone matched for" << windowId << "zone=" << result.zoneId;
            return result;
        }
    }

    return SnapResult::noSnap();
}

int SnapEngine::currentVirtualDesktop() const
{
    return m_virtualDesktopManager ? m_virtualDesktopManager->currentDesktop() : 0;
}

QString SnapEngine::currentActivity() const
{
    return m_layoutManager ? m_layoutManager->currentActivity() : QString();
}

bool SnapEngine::isEnabled() const noexcept
{
    // Snapping's global master toggle is the engine's enabled state — there is
    // no per-screen "is snapping active here" notion (that is the layout-mode
    // router's job). When false, the whole snap subsystem is off, mirroring
    // AutotileEngine::isEnabled() reporting autotile's effective state.
    auto* s = snapSettings();
    return s && s->snappingEnabled();
}

} // namespace PhosphorSnapEngine
