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
// A matched SnapToZone placement rule has highest priority: it overrides any
// remembered placement. It is APPLIED inside the WindowPlacementStore block, AFTER
// the store re-binds the window's record to the live id, so the rule decides WHERE
// the window snaps while the store still preserves its float-back geometry for a
// later Meta+F. With no rule, the store restores the snapped or floated record
// (snapping's two states); with neither, the fallback chain runs (1. empty zone,
// 2. last zone), and a no-match window defaults to floated. Persisted session
// restore is served by the store block, not a chain level.
//
// Mostly decision logic: returns a SnapResult for the caller to apply geometry.
// Side effects: consumePendingAssignment; marks floated windows floating
// (setFloatingOnScreen + windowFloatingChanged) on the floated-restore branch and
// on the no-match default; geometryRestoreRequested for floated position restore.
// The caller (windowOpened or WTA D-Bus facade) handles zone assignment and
// geometry application.
//
// Screen mode semantics:
//   - SNAPPED placement-store restore may cross-screen migrate: a snapped record's
//     own screenId can route a window to a different screen, and the store's take()
//     accept predicate / snapped-branch screen check keep an autotile-mode screen
//     from being snapped onto (autotile on that screen will own it). SnapToZone
//     placement rules (chain level 1) resolve on the window's CURRENT screen — a
//     screen constraint is expressed as a ScreenId match on the rule itself, not a
//     cross-screen move. FLOATED records are screen-local — a float-back is restored
//     only when the window reopens on its recorded screen, never moved across
//     monitors (the accept predicate gates floated records on the opening screen).
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
    // no window may be auto-snapped on open — not via SnapToZone placement rules,
    // session restore, empty-zone auto-assign, or last-used-zone. The screen-mode gate
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

    // Highest-priority placement: a matched SnapToZone rule. An explicit "this app
    // snaps to these zones" directive outranks ANY remembered placement — a floated
    // position or a prior snap to a different zone. Resolved up front (after the
    // autotile screen-mode gate, so autotile screens still own their windows) but
    // APPLIED below, AFTER the placement store has re-bound the window's record: the
    // rule overrides WHERE the window goes, while the store still preserves the
    // window's float-back geometry so a later Meta+F returns it to its remembered
    // free position rather than the zone rect. When the rule's own target context is
    // disabled, it does NOT win and we fall through to the normal store restore.
    const SnapResult placementRuleResult = calculateSnapToPlacementRule(windowId, screenId, sticky);
    const bool placementRuleWins = placementRuleResult.shouldSnap
        && (!m_shouldRestorePredicate || m_shouldRestorePredicate(placementRuleResult.screenId));

    // Unified placement store — the single authoritative restore record for this
    // window. Consulted before the legacy "already has assignment" skip and the
    // snap/float chains (but after a matched SnapToZone rule, above). This
    // ordering is load-bearing: on a daemon-only
    // restart the old WindowZoneAssignmentsFull is still loaded, so a window that
    // was FLOATED (floating keeps its zone assignment) comes back isWindowSnapped()
    // == true and would hit the legacy skip below — never floating. Consulting the
    // store first lets the floated record override that stale assignment.
    // Mutual exclusivity (one record per window) means a snapped window never
    // resurrects a stale float (the floated→snapped→login bug). Only snap-owned
    // records on a matching screen are handled here; autotile records are left for
    // autotile's own open path. Falls through to the legacy skip + chain when the
    // store has no record (windows persisted under the old keys before migration).
    if (m_windowTracker) {
        const QString appId = m_windowTracker->currentAppIdFor(windowId);
        // Whether a FLOATED record for THIS window may restore its recorded global
        // position on open — the daemon resolves the
        // `snappingRestoreFloatedWindowsOnLogin` setting plus the per-window
        // RestorePosition rule. When true the floated record is eligible
        // regardless of the opening screen, so a window KWin reopened on the wrong
        // monitor returns to its recorded one (stored geometry is global, so
        // re-applying it lands on the original output). When false the historical
        // opening-screen gate stands. Snapped records are never governed by this.
        const bool restoreFloatedPosition = m_restorePositionPredicate && m_restorePositionPredicate(windowId);
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
                // for autotile). A floated position is ALWAYS screen-local: it is
                // eligible only when the window opens on its recorded monitor (the
                // gate below). Float restore never MOVES a window across monitors.
                if (p.slotFor(engineId()).state == WindowPlacement::stateSnapped()) {
                    return recordedSnapScreenIsSnapping(p);
                }
                // A contentless {floating, no geometry, no zones} residue record
                // (left by an earlier-closed instance captured frame-less) has nothing
                // to restore. Never CONSUME it: at MaxPerApp entries per app it would
                // otherwise be taken ahead of the window's real placement (captured
                // last at save time, so it sits at the back of the FIFO), and the
                // window would return neither to its zone nor its saved free/float
                // position. Rejecting it here also lets a genuinely-new window fall
                // through to the auto-snap chain instead of consuming a dead record
                // and short-circuiting to no-snap. (Mirrors AutotileEngine's restore
                // accept predicate, which likewise only consumes records with a real
                // autotile slot.)
                if (!p.hasRestorableContent()) {
                    return false;
                }
                // A floated record is SCREEN-LOCAL: eligible only when the window
                // opens on the monitor it was recorded on (or an unscreened record).
                // Float restore must NEVER move a window to a different monitor —
                // a stale float record left by an earlier instance on another output
                // (matched via the appId FIFO, not an exact-windowId match) would
                // otherwise teleport a freshly-launched window onto a monitor it never
                // occupied, and the wrong-monitor capture that follows re-cements the
                // bad record into a self-perpetuating cross-monitor jump. The opening
                // monitor is owned by KWin's placement / session restore; PlasmaZones
                // only restores the floated POSITION within that monitor (gated on the
                // restore-floated opt-in at the geometry-move step below).
                return p.screenId.isEmpty() || p.screenId == screenId;
            },
            [&](const WindowPlacement& p) {
                // Among an app's FIFO records, restore a snapped placement on a
                // still-snapping screen ahead of an unsnapped (free/floating) sibling
                // that is merely older — snapping is the stronger restore intent, and
                // this keeps a cross-screen snapped record from losing to an older
                // free/floating record (or being passed over so a snap-screen open
                // consumes and destroys an autotile-owned record it cannot use).
                // Contentless residue is already excluded by the accept predicate, so
                // the second (merely-accepted) pass only ever sees real placements.
                return recordedSnapScreenIsSnapping(p);
            });
        if (rec) {
            // Re-record the restored placement bound to the LIVE windowId so the
            // window's float-back geometry (freeGeo) and the OTHER engine's slot
            // survive the reopen. This is load-bearing for logout/login: KWin assigns
            // a NEW uuid at login, so the record matches by appId FIFO (not
            // uuid-exact). Without re-binding, a FIFO reopen CONSUMES the record and
            // the float-back is lost — floating the window after login then finds no
            // recorded free position and strands it on its zone (which a later capture
            // records as a poisoned zone-rect float-back). Re-binding appends the
            // record under the live uuid (newest in the appId bucket), so a SECOND
            // instance of the same app still takes an OLDER sibling record first on its
            // own reopen — multi-instance FIFO distribution is preserved.
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
            rec->windowId = windowId;
            m_windowTracker->placementStore().record(*rec);

            // The record (and its float-back geometry) is now re-bound to the live
            // window. A matched SnapToZone rule overrides the remembered placement
            // here — the window snaps to the rule's zones while its freeGeo survives
            // in the record for a later Meta+F float.
            if (placementRuleWins) {
                qCInfo(PhosphorSnapEngine::lcSnapEngine)
                    << "resolveWindowRestore: placement rule overrides stored record for" << windowId
                    << "zones=" << placementRuleResult.zoneIds << "(freeGeo preserved=" << freeGeo << ")";
                return placementRuleResult;
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
            } else {
                // FLOATED restore. Snapping has only two states — snapped (above) or
                // floated — so any non-snapped record is floated. This also absorbs
                // legacy `free` records (the retired third state): a `free` slot
                // persisted by an older build is restored as floating, the single
                // point where that mapping happens.
                //
                // This branch deliberately does NOT consult m_shouldRestorePredicate.
                // That gate refuses to auto-SNAP a window onto a context the user
                // disabled snapping for; a floated window is not being snapped into a
                // zone, so restoring its floating state is correct regardless.
                m_snapState->setFloatingOnScreen(windowId, restoreScreen, currentVirtualDesktop());
                if (!slot.zoneIds.isEmpty()) {
                    // A window floated FROM a snapped state carries its pre-float zones
                    // for the resnap path; a never-snapped floated window has none.
                    m_snapState->addPreFloatZone(windowId, slot.zoneIds);
                    m_snapState->addPreFloatScreen(windowId, restoreScreen);
                }
                // The window is floating regardless of whether a position was recorded
                // — tell the compositor unconditionally (matching toggleWindowFloat /
                // setWindowFloat / handoffReceive). The geometry MOVE, however, is
                // gated on the unsnapped-position-restore opt-in (global setting +
                // per-window RestorePosition rule) for ALL floated windows: when off,
                // the window comes back floating but stays where KWin placed it.
                Q_EMIT windowFloatingChanged(windowId, true, restoreScreen);
                if (restoreFloatedPosition && freeGeo.isValid()) {
                    Q_EMIT geometryRestoreRequested(windowId, freeGeo, restoreScreen);
                }
                qCInfo(PhosphorSnapEngine::lcSnapEngine)
                    << "resolveWindowRestore: placement(floated) for" << windowId << "->" << freeGeo
                    << "move=" << (restoreFloatedPosition && freeGeo.isValid());
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

    // No stored record matched above, so there is no float-back geometry to
    // inherit — but a matched SnapToZone rule still wins for a fresh window.
    if (placementRuleWins) {
        qCInfo(PhosphorSnapEngine::lcSnapEngine)
            << "resolveWindowRestore: placement rule matched (no stored record) for" << windowId
            << "zones=" << placementRuleResult.zoneIds;
        return placementRuleResult;
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
    // BEFORE the exclusion lookup and the calculate* chain so placement rules,
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
    // by placement rules, session restore, empty zone, or last zone features.
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

    // Floating windows are never auto-snapped — skip the chain. (A skip guard, not
    // a fallback level.) No OSD feedback here: this is the automatic window-open /
    // restore path, not an interactive float toggle — the user did not act, so a
    // "floated" toast would be spurious (and would spam at login, where the
    // retry net re-resolves every floating window, including the now-default
    // floated ones). Interactive feedback lives in toggleWindowFloat.
    if (m_snapState->isFloating(windowId)) {
        qCInfo(PhosphorSnapEngine::lcSnapEngine)
            << "resolveWindowRestore: window" << windowId << "is floating, skipping snap";
        return SnapResult::noSnap();
    }

    // A matched "Float this app" window rule opens the window floating, exactly
    // as if the user had toggled float. Runs after the exclusion gate (excluded
    // windows return earlier), the already-floating guard, and the persisted-
    // placement restore (a window the user previously snapped reopens snapped —
    // that restore returns before reaching here), but before the auto-snap chain —
    // so a rule-floated window never auto-snaps to a zone.
    // Mirrors the no-match default-float terminal at the end of this function.
    if (m_floatPredicate && m_floatPredicate(windowId)) {
        m_snapState->setFloatingOnScreen(windowId, screenId, currentVirtualDesktop());
        Q_EMIT windowFloatingChanged(windowId, true, screenId);
        qCInfo(PhosphorSnapEngine::lcSnapEngine) << "resolveWindowRestore:" << windowId << "floated by rule";
        return SnapResult::noSnap();
    }

    // (SnapToZone placement rules are resolved BEFORE the placement store, near
    // the top of this function — an explicit rule outranks any remembered
    // placement. See the "highest-priority restore" block above.)

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

    // No auto-snap matched on a snap-mode screen — the window defaults to FLOATED
    // (snapping's only non-snapped state; the retired `free` default is gone). Mark
    // it floating so it has a definite state and the float toggle / minimize / save
    // paths treat it like autotile's floated windows. Reached ONLY here: the
    // autotile-defer, disabled-context, exclusion, already-floating and
    // already-snapped guards above all return earlier, and the autotile-caller
    // short-circuit returns before the empty/last-zone chain — so this is always a
    // genuine snap-mode window with no zone match.
    m_snapState->setFloatingOnScreen(windowId, screenId, currentVirtualDesktop());
    Q_EMIT windowFloatingChanged(windowId, true, screenId);
    qCInfo(PhosphorSnapEngine::lcSnapEngine)
        << "resolveWindowRestore:" << windowId << "no snap match — defaulting to floated on" << screenId;
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
    // (a leftover pre-tile unmanaged rect gets reported as a floated record) and
    // clobber that frozen snap record, breaking per-mode float independence. Each
    // engine remembers
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
        p.screenId = m_snapState->screenForWindow(windowId);
    } else {
        // Snapping has only two states — snapped (above) or floated. An unmanaged
        // window on a snap-mode screen is FLOATED (the retired `free` state). The
        // orchestrator fills freeGeometryByScreen from the live frame; a contentless
        // capture (no frame) is dropped by hasRestorableContent() so geometry-less
        // floated residue never floods the per-app FIFO.
        slot.state = WindowPlacement::stateFloating();
        p.screenId = screenForTrackedWindow(windowId);
    }
    p.engines.insert(engineId(), slot);
    return p;
}

} // namespace PhosphorSnapEngine
