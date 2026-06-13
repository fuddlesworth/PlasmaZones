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
// resolveWindowRestore — WindowPlacementStore restore + auto-snap fallback chain
//
// Consults the unified WindowPlacementStore first (snapped / floating / free
// record); if none applies, runs the fallback chain (1. app rules, 2. empty
// zone, 3. last zone). Persisted session restore is now served by the store
// block above, not a chain level.
//
// Mostly decision logic: returns a SnapResult for the caller to apply geometry.
// Side effects: consumePendingAssignment, navigationFeedback emit (floating
// windows). The caller (windowOpened or WTA D-Bus facade) handles zone
// assignment and geometry application.
//
// Screen mode semantics:
//   - The placement-store restore and app rules (chain level 1) may
//     cross-screen migrate: a stored record's own screenId or an app rule can
//     route a window to a different screen, and the store's take() accept
//     predicate / snapped-branch screen check keep an autotile-mode screen from
//     being snapped onto (autotile on that screen will own it).
//   - The empty-zone (level 2) and last-zone (level 3) fallbacks inherently use
//     the caller screen as the target, so they are ONLY valid when the caller's
//     screen is in snap mode. On autotile screens they're short-circuited —
//     stale snap zones on a now-autotile screen must not bleed into placement.
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

    using PhosphorEngine::WindowPlacement;

    // A snapped record's RECORDED screen (its own screenId, or the opening screen
    // when unscreened) is what governs whether it may snap-restore — not the screen
    // the window happens to open on. True only when that recorded screen is in
    // snapping mode, resolved in the RECORD'S OWN (desktop, activity) context.
    // Keying on the record's context — not the live current desktop/activity — is
    // load-bearing for cross-engine agreement: AutotileEngine::windowOpened runs the
    // reciprocal gate over the SAME record fields, so both engines compute an
    // identical verdict regardless of per-screen virtual-desktop overrides. (No
    // layout manager → permissive, matching the unit-test path.)
    const auto recordedSnapScreenIsSnapping = [&](const WindowPlacement& p) {
        if (p.slotFor(engineId()).state != WindowPlacement::stateSnapped()) {
            return false;
        }
        if (!m_layoutManager) {
            return true;
        }
        const QString rec = p.screenId.isEmpty() ? screenId : p.screenId;
        return m_layoutManager->modeForScreen(rec, p.virtualDesktop, p.activity)
            == PhosphorZones::AssignmentEntry::Mode::Snapping;
    };

    // Screen-mode ownership gate (BEFORE the store branch). A window opening on an
    // AUTOTILE-mode screen is normally owned by the autotile engine — snap must not
    // restore a stale record onto it (which would both wrongly snap an autotile
    // window AND overwrite its autotile record via the store's mutual-exclusivity
    // invariant, defeating autotile's own float restore in insertWindow()).
    // EXCEPTION: a window may carry a SNAPPED record whose RECORDED screen is itself
    // in snapping mode — i.e. it was snapped on another (snap) monitor and KWin
    // merely opened the session window on this autotile screen. That window must
    // still restore cross-screen to its snap monitor (mirrors main, which gated the
    // defer on the SAVED screen, not the opening one). So defer ONLY when no such
    // cross-screen snap restore is pending; the store branch below then consumes the
    // snapped record and restores it to its recorded screen. A same-screen snapped
    // record (recorded screen == this autotile screen) is NOT snapping, so the peek
    // misses and we correctly defer, leaving the record for autotile.
    if (m_layoutManager
        && m_layoutManager->modeForScreen(screenId, currentVirtualDesktop(), currentActivity())
            != PhosphorZones::AssignmentEntry::Mode::Snapping) {
        bool crossScreenSnapRestorePending = false;
        if (m_windowTracker) {
            const QString appId = m_windowTracker->currentAppIdFor(windowId);
            crossScreenSnapRestorePending =
                m_windowTracker->placementStore().peek(windowId, appId, recordedSnapScreenIsSnapping).has_value();
        }
        if (!crossScreenSnapRestorePending) {
            qCDebug(PhosphorSnapEngine::lcSnapEngine)
                << "resolveWindowRestore:" << windowId << "opens on non-snap-mode screen" << screenId
                << "— snap defers to the owning engine";
            return SnapResult::noSnap();
        }
        qCDebug(PhosphorSnapEngine::lcSnapEngine)
            << "resolveWindowRestore:" << windowId << "opens on non-snap-mode screen" << screenId
            << "but carries a cross-screen snap restore — not deferring";
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
        const QString appId = m_windowTracker->currentAppIdFor(windowId);
        // Whether an UNSNAPPED record (free / snap-floated) for THIS window may
        // restore its recorded global position on open — the daemon resolves the
        // global `restoreUnsnappedWindowsOnLogin` setting plus the per-window
        // RestorePosition rule. When true the free/floating record is eligible
        // regardless of the opening screen, so a window KWin reopened on the wrong
        // monitor returns to its recorded one (stored geometry is global, so
        // re-applying it lands on the original output). When false the historical
        // opening-screen gate stands. Snapped records are never governed by this.
        const bool restoreUnsnappedPosition = m_restorePositionPredicate && m_restorePositionPredicate(windowId);
        // ONE record per window (both engines' slots + the shared free geometry).
        // take() consumes it (multi-instance FIFO); we then re-record it bound to
        // the LIVE windowId so the OTHER engine's slot and the per-screen free
        // geometry survive — without this, a snap-screen open would wipe the
        // window's autotile slot. Binding the live id is also the consumption: a
        // second instance of the same app no longer uuid-matches and takes the
        // next FIFO entry.
        auto rec = m_windowTracker->placementStore().take(
            windowId, appId,
            [&](const WindowPlacement& p) {
                // A SNAPPED record carries its own authoritative screen + zone, so
                // it is eligible regardless of which monitor the window happens to
                // open on. KWin can place a session window on a different output at
                // login (e.g. a window snapped on screen A reopens on screen B); the
                // window must still return to the screen + zone it was snapped to.
                // This mirrors main's calculateRestoreFromSession, which keyed the
                // restore by appId and honoured the saved screen (`savedScreen =
                // entry.screenId ?: callerScreen`), explicitly supporting cross-screen
                // restore migration. Gating snapped records on the OPENING screen —
                // which the unified-store rewrite introduced — stranded such windows
                // on the wrong monitor, unsnapped. The RECORDED screen must still be
                // in snapping mode, though: a snapped record whose own screen is now
                // autotile-owned must not snap-restore there (the early gate leaves it
                // for autotile). A floating/free position is screen-local UNLESS the
                // user opted into unsnapped-position restore (global setting / rule),
                // in which case the record is eligible cross-screen so the window
                // returns to its recorded monitor; otherwise it stays gated on the
                // opening screen.
                if (p.slotFor(engineId()).state == WindowPlacement::stateSnapped()) {
                    return recordedSnapScreenIsSnapping(p);
                }
                if (restoreUnsnappedPosition) {
                    return true;
                }
                return p.screenId.isEmpty() || p.screenId == screenId;
            },
            [&](const WindowPlacement& p) {
                // Among an app's FIFO records, restore a record that actually has
                // something to restore — a snapped placement on a still-snapping
                // screen, or any record carrying real free/float geometry — ahead of a
                // contentless {free, no geometry} sibling that is merely older.
                // Otherwise a stale residue record (empty screen, no zone, no geometry)
                // is consumed first and the window never returns to its zone OR its
                // saved floating/free position. This is the load-bearing fix for
                // unsnapped-position restore across logout/login: the floated window's
                // real record, captured last at save time, sits at the back of the
                // FIFO and would lose to older residue without this preference.
                return recordedSnapScreenIsSnapping(p) || p.anyFreeGeometry().isValid();
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
            // The SCREEN-LOCAL recorded position for restoreScreen — deliberately NOT
            // the anyFreeGeometry() cross-screen fallback. A free/floating reposition
            // emits global compositor coordinates: they only land the window back on
            // restoreScreen if the rect was captured on restoreScreen. Applying some
            // other screen's rect would put the window on a third monitor while the
            // floating-on-screen tracking (set to restoreScreen) says otherwise — a
            // visible/state desync. If restoreScreen has no recorded position, there is
            // nothing meaningful to restore, so the move is skipped. (Snapped restore
            // places by zone geometry and never consults this.)
            const QRect freeGeo = rec->freeGeometryFor(restoreScreen);
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
                // The window is floating regardless of whether a free position was
                // recorded — tell the compositor unconditionally (matching
                // toggleWindowFloat / setWindowFloat / handoffReceive). Only the
                // geometry move is gated on a valid recorded position.
                Q_EMIT windowFloatingChanged(windowId, true, restoreScreen);
                if (freeGeo.isValid()) {
                    Q_EMIT geometryRestoreRequested(windowId, freeGeo, restoreScreen);
                }
                qCInfo(PhosphorSnapEngine::lcSnapEngine)
                    << "resolveWindowRestore: placement(floating) for" << windowId << "->" << freeGeo;
                return SnapResult::noSnap();
            } else if (slot.state == WindowPlacement::stateFree()) {
                // free geometry already lives in the record; nothing to re-seed.
                // When the user opted into unsnapped-position restore, move the
                // window back to its recorded global position (which, being in
                // compositor-global coords, returns it to its original monitor —
                // KWin may have reopened the session window elsewhere). The window
                // stays genuinely unmanaged; this is a one-shot placement, not a
                // snap. Eligibility above already required restoreUnsnappedPosition
                // to consume a cross-screen record, but a same-screen free record is
                // consumed unconditionally — so gate the move itself here too.
                if (restoreUnsnappedPosition && freeGeo.isValid()) {
                    Q_EMIT geometryRestoreRequested(windowId, freeGeo, restoreScreen);
                    qCInfo(PhosphorSnapEngine::lcSnapEngine)
                        << "resolveWindowRestore: placement(free) reposition for" << windowId << "->" << freeGeo;
                }
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

    // Floating windows should not be auto-snapped — emit OSD feedback. (A skip
    // guard, not a fallback level.)
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

    // (Persisted session restore is now served entirely by the unified
    // WindowPlacementStore block at the top of this function — a snapped window
    // reopens from its WindowPlacement record. It is no longer a chain level; the
    // legacy PendingRestoreQueues / calculateRestoreFromSession path is gone.)

    // Levels 2 and 3 inherently target the caller's screen (the empty-zone /
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

    // 2. Auto-assign to empty zone
    {
        SnapResult result = calculateSnapToEmptyZone(windowId, screenId, sticky);
        if (result.shouldSnap) {
            qCInfo(PhosphorSnapEngine::lcSnapEngine)
                << "resolveWindowRestore: emptyZone matched for" << windowId << "zone=" << result.zoneId;
            return result;
        }
    }

    // 3. Snap to last zone (final fallback)
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
