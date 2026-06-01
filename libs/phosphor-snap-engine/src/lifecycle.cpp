// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorSnapEngine/SnapEngine.h>
#include <PhosphorSnapEngine/SnapState.h>
#include <PhosphorZones/AssignmentEntry.h>

#include <QJsonArray>
#include <QJsonValue>
#include <PhosphorSnapEngine/ISnapSettings.h>
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

    // Mark this window as reported by the effect (confirmed live), distinguishing
    // live windows from stale identity entries after a KWin restart (UUIDs change).
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
    // appropriate. When the result came from the placement store,
    // resolveWindowRestore already consumed (took) the record. Last-used-zone
    // update is skipped by AutoRestored intent.
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
//   - The placement-store restore (level 2) and app rules (level 1) may
//     cross-screen migrate: a stored record's own screenId or an app rule can
//     route a window to a different screen, and the store's take() accept
//     predicate / snapped-branch screen check keep an autotile-mode screen from
//     being snapped onto (autotile on that screen will own it).
//   - Levels 3 (empty zone) and 4 (last zone) inherently use the caller
//     screen as the target, so they are ONLY valid when the caller's screen
//     is in snap mode. On autotile screens they're short-circuited — stale
//     snap zones on a now-autotile screen must not bleed into placement.
// ═══════════════════════════════════════════════════════════════════════════════

SnapResult SnapEngine::resolveWindowRestore(const QString& windowId, const QString& screenId, bool sticky,
                                            PhosphorEngine::WindowKind kind)
{
    Q_UNUSED(kind) // window kind no longer gates restore — the store record carries it
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

    // Screen-mode ownership gate (BEFORE the store branch). A window opening on an
    // AUTOTILE-mode screen is owned by the autotile engine — snap must not restore
    // it, even when a stale snap record (snapped/floated/free from a prior snap-mode
    // session on this screen) still exists in the store. Without this, snap commits
    // that stale record on reopen, which both wrongly snaps an autotile window AND
    // overwrites its autotile record via the store's mutual-exclusivity invariant,
    // defeating autotile's own float restore in insertWindow(). Each engine restores
    // only on the screens its mode owns; floating in one mode is independent of the
    // other. (Autotile never steals snap-screen windows — it only processes windows
    // on its active screens — so only the snap side needs this guard.)
    if (m_layoutManager
        && m_layoutManager->modeForScreen(screenId, currentVirtualDesktop(), currentActivity())
            != PhosphorZones::AssignmentEntry::Mode::Snapping) {
        qCDebug(PhosphorSnapEngine::lcSnapEngine)
            << "resolveWindowRestore:" << windowId << "opens on non-snap-mode screen" << screenId
            << "— snap defers to the owning engine";
        return SnapResult::noSnap();
    }

    // Unified placement store — the single authoritative restore record for this
    // window. Consulted FIRST, before the legacy "already has assignment" skip and
    // the snap/float chains. This ordering is load-bearing: on a daemon-only
    // restart the old WindowZoneAssignmentsFull is still loaded, so a window that
    // was FLOATED (floating keeps its zone assignment) comes back isWindowSnapped()
    // == true and would hit the legacy skip below — never floating. Consulting the
    // store first lets the floated/free record override that stale assignment.
    // Mutual exclusivity (one record per window) means a snapped window never
    // resurrects a stale float (the floated→snapped→login bug). Only snap-owned
    // records on a matching screen are handled here; autotile records are left for
    // autotile's own open path. Falls through to the legacy skip + chain when the
    // store has no record (windows persisted under the old keys before migration).
    if (m_windowTracker) {
        using PhosphorEngine::WindowPlacement;
        const QString appId = m_windowTracker->currentAppIdFor(windowId);
        // ONE record per window (both engines' slots + the shared free geometry).
        // take() consumes it (multi-instance FIFO); we then re-record it bound to
        // the LIVE windowId so the OTHER engine's slot and the per-screen free
        // geometry survive — without this, a snap-screen open would wipe the
        // window's autotile slot. Binding the live id is also the consumption: a
        // second instance of the same app no longer uuid-matches and takes the
        // next FIFO entry.
        auto rec = m_windowTracker->placementStore().take(windowId, appId, [&](const WindowPlacement& p) {
            return p.screenId.isEmpty() || p.screenId == screenId;
        });
        if (rec) {
            // Same window across a daemon restart (uuid-exact) vs a reopened instance
            // (appId FIFO). Only the former re-records — keeping the FULL record (the
            // other engine's slot + per-screen free geometry) alive across further
            // restarts. A FIFO reopen CONSUMES instead: re-recording it would let a
            // second instance of the same app steal this placement on its own reopen.
            const bool wasExact = (rec->windowId == windowId);
            const QString restoreScreen = rec->screenId.isEmpty() ? screenId : rec->screenId;
            const PhosphorEngine::EngineSlot slot = rec->slotFor(engineId());
            QRect freeGeo = rec->freeGeometryFor(restoreScreen);
            if (!freeGeo.isValid()) {
                freeGeo = rec->anyFreeGeometry();
            }
            if (wasExact) {
                m_windowTracker->placementStore().record(*rec);
            }

            if (slot.state == WindowPlacement::stateSnapped()) {
                // A stored snap is still subject to the disabled-context gate.
                if (!m_shouldRestorePredicate || m_shouldRestorePredicate(restoreScreen)) {
                    const QStringList zoneIds = slot.zoneIds;
                    const QRect geo =
                        zoneIds.isEmpty() ? QRect() : m_windowTracker->resolveZoneGeometry(zoneIds, restoreScreen);
                    if (geo.isValid()) {
                        // freeGeo already lives in the record (the single float-back
                        // store) — a later float toggle reads it directly; nothing to
                        // re-seed into a separate per-engine store.
                        // Daemon-only restart already loaded the exact assignment:
                        // re-committing would be a redundant apply. If the window is
                        // already snapped to these zones, just consume the pending
                        // entry and no-op — the float-back re-seed above is the only
                        // thing that needed doing.
                        if (m_snapState->isWindowSnapped(windowId)
                            && m_snapState->zonesForWindow(windowId) == zoneIds) {
                            m_windowTracker->consumePendingAssignment(windowId);
                            qCInfo(PhosphorSnapEngine::lcSnapEngine)
                                << "resolveWindowRestore: placement(snapped) already assigned, no-op for" << windowId;
                            return SnapResult::noSnap();
                        }
                        qCInfo(PhosphorSnapEngine::lcSnapEngine) << "resolveWindowRestore: placement(snapped) for"
                                                                 << windowId << "->" << geo << "freeGeo=" << freeGeo;
                        return SnapResult{true, geo, zoneIds.first(), zoneIds, restoreScreen};
                    }
                }
                // Disabled context or zone gone (layout edit) → fall through to
                // the legacy chain below.
            } else if (slot.state == WindowPlacement::stateFloating()) {
                // NOTE: the floating (and free, below) branch deliberately does NOT
                // consult m_shouldRestorePredicate. That gate refuses to auto-SNAP a
                // window onto a context the user disabled snapping for; a floating/free
                // window is not being snapped into a zone, so restoring its floating
                // position is correct regardless of the snap-disable state.
                m_snapState->setFloatingOnScreen(windowId, restoreScreen, currentVirtualDesktop());
                if (!slot.zoneIds.isEmpty()) {
                    m_snapState->addPreFloatZone(windowId, slot.zoneIds);
                    m_snapState->addPreFloatScreen(windowId, restoreScreen);
                }
                if (freeGeo.isValid()) {
                    Q_EMIT geometryRestoreRequested(windowId, freeGeo, restoreScreen);
                    Q_EMIT windowFloatingChanged(windowId, true, restoreScreen);
                }
                qCInfo(PhosphorSnapEngine::lcSnapEngine)
                    << "resolveWindowRestore: placement(floating) for" << windowId << "->" << freeGeo;
                return SnapResult::noSnap();
            } else if (slot.state == WindowPlacement::stateFree()) {
                // free geometry already lives in the record; nothing to re-seed.
                return SnapResult::noSnap();
            }
        }
    }

    // Defensive guard: a window that is already snapped (its WindowPlacement
    // record was normally applied by the store block above, which committed and
    // no-op'd) must not fall through to the auto-snap policy chain and get
    // re-snapped into a different zone. Unreachable in the common path (capture
    // keeps a record for every snapped window), but cheap insurance.
    if (m_snapState->isWindowSnapped(windowId)) {
        qCDebug(PhosphorSnapEngine::lcSnapEngine)
            << "resolveWindowRestore:" << windowId << "already snapped, skipping auto-snap";
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
    // tracker. isAppIdExcluded resolves through the unified RuleEvaluator
    // (daemon-flavour AppIdMatches Exclude rules) — the same match model the
    // effect uses, replacing the hand-rolled appIdMatches loops.
    if (m_windowTracker && isAppIdExcluded(m_windowTracker->currentAppIdFor(windowId))) {
        qCInfo(PhosphorSnapEngine::lcSnapEngine) << "resolveWindowRestore:" << windowId << "excluded by rule";
        return SnapResult::noSnap();
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

    // 2. Persisted session restore is now served entirely by the unified
    // WindowPlacementStore block at the top of this function (a snapped window
    // reopens from its WindowPlacement record). The legacy
    // PendingRestoreQueues / calculateRestoreFromSession path is gone.

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

// ═══════════════════════════════════════════════════════════════════════════════
// Unified placement model — capture / restore
// ═══════════════════════════════════════════════════════════════════════════════

std::optional<PhosphorEngine::WindowPlacement> SnapEngine::capturePlacement(const QString& windowId) const
{
    using PhosphorEngine::WindowPlacement;
    if (windowId.isEmpty() || !m_snapState) {
        return std::nullopt;
    }

    // Per-mode ownership: snap captures a window only while its CURRENT screen is in
    // snapping mode. On an autotile-mode screen the window's snap state is FROZEN
    // memory (its last snap-mode placement, restored when the screen returns to
    // snapping) — autotile owns the window now. Re-capturing here would over-claim
    // (a leftover pre-tile unmanaged rect gets reported as "free") and clobber that
    // frozen snap record, breaking per-mode float independence. Each engine remembers
    // the window's state in its OWN mode; returning nullopt leaves the snap record
    // untouched.
    if (m_layoutManager) {
        const QString effScreen = screenForTrackedWindow(windowId);
        if (!effScreen.isEmpty()
            && m_layoutManager->modeForScreen(effScreen, currentVirtualDesktop(), currentActivity())
                != PhosphorZones::AssignmentEntry::Mode::Snapping) {
            return std::nullopt;
        }
    }

    WindowPlacement p;
    p.windowId = windowId;
    p.appId = m_windowTracker ? m_windowTracker->currentAppIdFor(windowId) : QString();
    p.virtualDesktop = currentVirtualDesktop();
    p.activity = currentActivity();

    // The slot carries only the snap engine's STATE + slot reference (zone IDs) —
    // NEVER a rectangle. The shared free/float geometry is set by the capture
    // orchestrator (WTA::captureWindowPlacement) from the live frame, and ONLY when
    // the state is free/floating, so a zone rect can never become the float-back.
    PhosphorEngine::EngineSlot slot;
    // Floating is checked BEFORE snapped: a floated-from-snap window keeps its
    // zone assignment (so a float-toggle can resnap it), so isWindowSnapped()
    // stays true while it floats. The active runtime state is floating — record
    // that, with the pre-float zones carried in the slot for the resnap path.
    if (m_snapState->isFloating(windowId)) {
        slot.state = WindowPlacement::stateFloating();
        slot.zoneIds = m_snapState->preFloatZones(windowId);
        p.screenId = screenForTrackedWindow(windowId);
    } else if (m_snapState->isWindowSnapped(windowId)) {
        slot.state = WindowPlacement::stateSnapped();
        slot.zoneIds = m_snapState->zonesForWindow(windowId);
        p.screenId = m_snapState->screenAssignments().value(windowId);
    } else {
        // Unmanaged on a snap-mode screen — a genuinely free window. The orchestrator
        // fills freeGeometryByScreen from the live frame (a real un-managed position).
        slot.state = WindowPlacement::stateFree();
        p.screenId = screenForTrackedWindow(windowId);
    }
    p.engines.insert(engineId(), slot);
    return p;
}

} // namespace PhosphorSnapEngine
