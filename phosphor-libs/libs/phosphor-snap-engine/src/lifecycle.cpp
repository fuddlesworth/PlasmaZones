// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorSnapEngine/SnapEngine.h>
#include <PhosphorSnapEngine/SnapState.h>
#include <PhosphorZones/AssignmentEntry.h>

#include <QJsonArray>
#include <QJsonValue>
#include <PhosphorSnapEngine/ISnapSettings.h>
#include <PhosphorZones/LayoutRegistry.h>
#include <PhosphorZones/LayoutUtils.h>
#include <PhosphorScreens/Manager.h>
#include "snapenginelogging.h"

#include <optional>

namespace PhosphorSnapEngine {

using PhosphorEngine::SnapIntent;
using PhosphorEngine::SnapResult;

// ═══════════════════════════════════════════════════════════════════════════════
// windowOpened — delegates to resolveWindowRestore() and applies the result
// ═══════════════════════════════════════════════════════════════════════════════

// No daemon path calls this: the tiling adaptor's lifecycle engines are
// autotile and scrolling, and a snap window's open restore runs through the
// adaptor facade (SnapAdaptor::resolveWindowRestore) the KWin effect calls. The
// IPlacementEngine override stays for the interface's sake and for the tests.
void SnapEngine::windowOpened(const QString& windowId, const QString& screenId, int minWidth, int minHeight)
{
    Q_UNUSED(minWidth)
    Q_UNUSED(minHeight)

    if (windowId.isEmpty() || screenId.isEmpty()) {
        return;
    }

    // Guard: skip a window that is already snapped, so a second open never
    // double-assigns it.
    if (const SnapState* openState = stateForWindow(windowId); openState && openState->isWindowSnapped(windowId)) {
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
    // Mark on the store the window is about to be committed into (result.screenId),
    // so the auto-snapped flag lives with the window's other per-screen state.
    // Pinned to the result's desktop (a background-desktop restore) so the
    // mark lands in the store the commit below writes into.
    stateForWindowOnScreen(windowId, result.screenId, result.virtualDesktop)->markAsAutoSnapped(windowId);
    const QStringList zoneIds = result.zoneIds.isEmpty() ? QStringList{result.zoneId} : result.zoneIds;
    if (zoneIds.size() > 1) {
        commitMultiZoneSnap(windowId, zoneIds, result.screenId, SnapIntent::AutoRestored, result.virtualDesktop);
    } else {
        commitSnap(windowId, zoneIds.first(), result.screenId, SnapIntent::AutoRestored, result.virtualDesktop);
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
// (snapping's two states); with neither, the fallback chain runs (2. empty zone,
// 3. last zone — the SnapToZone rule is chain level 1, matching the body's
// numbering), and a no-match window defaults to floated. Persisted session
// restore is served by the store block, not a chain level.
//
// Mostly decision logic: returns a SnapResult for the caller to apply geometry.
// Side effects: marks floated windows floating
// (setFloatingOnScreen + windowFloatingChanged) on the floated-restore branch,
// the float-by-rule terminal and the no-match default; geometryRestoreRequested
// for the floated position restore; sizeRestoreRequested (via
// restoreFreeSizeForUnplaced) on a first-placement float verdict that does not
// move the window, so it gets its remembered free size back where it stands.
// The geometry and size emits precede windowFloatingChanged on every terminal:
// the effect's float handler releases first-frame suppression when no
// reposition is in flight, so the reposition has to be stamped first.
// The caller (windowOpened or WTA D-Bus facade) handles zone assignment and
// geometry application.
//
// Screen mode semantics (the reopen contract: restores happen where a window
// opens):
//   - A window opening on a screen another engine runs is that engine's; snap
//     restores nothing there.
//   - The window's OWN record (same instance) restores only on the virtual
//     screen it names. Recorded on any other screen, the window "left its
//     screen": nothing is restored, every engine slot of the record is
//     released, and no sibling record stands in.
//   - A FIFO record (another instance of the app) restores only on the opening
//     KWin output: on this very screen, or a SNAPPED record on another virtual
//     screen of the same output that also runs snapping. A FLOATED record is
//     screen-local: only on the screen it names.
//   - SnapToZone placement rules resolve on the window's CURRENT screen; a
//     screen constraint is a ScreenId match on the rule itself.
//   - The empty-zone (level 2) and last-zone (level 3) fallbacks target the
//     caller screen, so they run only when it is in snap mode, and never for a
//     window that left its screen (it takes the float default in place).
// ═══════════════════════════════════════════════════════════════════════════════

int SnapEngine::restoreDesktopFor(const QString& windowId, const PhosphorEngine::WindowPlacement& rec,
                                  const QString& restoreScreen) const
{
    // The desktop the window is actually being placed onto, which is not the
    // screen's current one when the compositor restores it onto a background
    // desktop: session restore puts each window back on its saved desktop,
    // and the registry has that stamped before this resolve runs. Reading the
    // screen instead granted the membership, stamped the assignment and
    // counted the zone occupancy on whatever desktop the user happened to be
    // looking at.
    const int screenDesktop = currentVirtualDesktopForScreen(restoreScreen);
    if (m_windowRegistry) {
        if (const auto ctx = m_windowRegistry->desktopContext(windowId)) {
            if (ctx->sticky.value_or(false)) {
                return screenDesktop; // on every desktop: the one in view is the one to apply
            }
            if (ctx->virtualDesktops.size() > 1) {
                return ctx->virtualDesktops.contains(screenDesktop) ? screenDesktop : ctx->virtualDesktops.first();
            }
            if (ctx->virtualDesktop > 0) {
                return ctx->virtualDesktop;
            }
        }
    }
    // No registry answer: a multi-desktop record's own desktop is the next
    // best witness, provided the record still names a zone there.
    if (rec.virtualDesktop >= 1 && rec.slotFor(engineId()).zonesByDesktop.contains(rec.virtualDesktop)) {
        return rec.virtualDesktop;
    }
    return screenDesktop;
}

SnapResult SnapEngine::resolveWindowRestore(const QString& windowId, const QString& screenId, bool sticky,
                                            PhosphorEngine::WindowKind kind, PhosphorEngine::RestoreReason reason)
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

    // Whether the RECORDED screen of a snapped record (its own screenId, or the
    // opening screen when unscreened) still runs snapping, asked in the LIVE
    // activity the restore keys its grant and seeds under (restoreActivity
    // below), whatever activity the record was captured under. (No layout
    // manager → permissive, matching the unit-test path.)
    const auto screenIsSnapping = [&](const QString& rec, int desktop) {
        return !m_layoutManager
            || m_layoutManager->modeForScreen(rec, desktop, currentActivity())
            == PhosphorZones::AssignmentEntry::Mode::Snapping;
    };
    const auto recordedSnapScreenIsSnapping = [&](const WindowPlacement& p) {
        if (p.slotFor(WindowPlacement::snapEngineId()).state != WindowPlacement::stateSnapped()) {
            return false;
        }
        return screenIsSnapping(p.screenId.isEmpty() ? screenId : p.screenId, p.virtualDesktop);
    };
    // A snapped record this open may restore: on the opening screen (or
    // unscreened) when that screen snaps, else only a FIFO record on another
    // virtual screen of the SAME output that snaps too (the reopen contract;
    // the shared predicate refuses the window's own record and other outputs).
    // A re-entry never re-snaps a window snap already tracks as floating.
    const auto snappedRecordRestorable = [&](const WindowPlacement& p) {
        if (reason != PhosphorEngine::RestoreReason::Open && isFloating(windowId)) {
            return false;
        }
        if (p.screenId.isEmpty() || p.screenId == screenId) {
            return recordedSnapScreenIsSnapping(p);
        }
        return PhosphorEngine::pendingCrossScreenSnapRestore(p, windowId, screenId,
                                                             [&](const QString& rec, int desktop, const QString&) {
                                                                 return screenIsSnapping(rec, desktop);
                                                             });
    };

    // Screen-mode gate (BEFORE the store branch). A window opening on a screen
    // in any non-snap mode (autotile or scrolling) is that engine's: snap must
    // not restore a record onto it (which would both wrongly snap a tiled
    // window AND overwrite its tiling record via the store's mutual-exclusivity
    // invariant). Under the reopen contract there is no exception for a
    // snapped record saved on another monitor: the window stays where it opens.
    if (!isSnapModeScreen(screenId)) {
        qCDebug(PhosphorSnapEngine::lcSnapEngine)
            << "resolveWindowRestore:" << windowId << "opens on non-snap-mode screen" << screenId
            << "— snap defers to the owning engine";
        return SnapResult::noSnap();
    }

    // The reopen contract on the window's OWN record: recorded on another screen
    // than the one it opens on (with a live slot), the window LEFT its screen
    // while nothing tracked it (a move while the daemon was down, a restart
    // after a move). It is not restored, every engine slot of the record is
    // released so no engine reads it as a home later, and no sibling record
    // stands in. A window snap still tracks is a re-resolve, not a leave. A
    // record on an output that is not present now stays parked (the wake-return
    // park reads it when the output comes back).
    const std::optional<WindowPlacement> own =
        m_windowTracker ? m_windowTracker->placementStore().peekExact(windowId) : std::nullopt;
    const PhosphorScreens::ScreenManager* screens = m_windowTracker ? m_windowTracker->screenManager() : nullptr;
    const bool leftScreen = own && PhosphorEngine::ownRecordLeftScreen(*own, screenId) && !isWindowTracked(windowId)
        && (!screens || screens->physicalScreenFor(own->screenId).isValid());

    // Highest-priority placement: a matched SnapToZone rule. An explicit "this app
    // snaps to these zones" directive outranks ANY remembered placement, a floated
    // position or a prior snap to a different zone. Resolved up front (after the
    // screen-mode gate, so tiled screens still own their windows) but APPLIED
    // below, AFTER the placement store has re-bound the window's record: the rule
    // overrides WHERE the window goes, while the store still preserves the
    // window's float-back geometry so a later Meta+F returns it to its remembered
    // free position rather than the zone rect. When the rule's own target context
    // is disabled, it does NOT win and we fall through to the normal store
    // restore. It stands down for a window that left its screen unless this is
    // its genuine open: after a restart, a re-enable or a sweep, a window the
    // user moved stays where they put it, rule or not.
    // Resolved ONCE: the same directive feeds the SnapToZone placement and
    // the desktop the float terminals pin their residence to. The adaptor
    // emitted the RouteToDesktop move before this resolve and the registry
    // is stamped only once the compositor has moved the window, so the
    // screen's current desktop is the one the window is LEAVING. A later
    // DesktopArrival re-drive re-homes a floating residence only from the
    // window's own floated record, so the open pass has to get it right.
    const bool ruleMayApply = !leftScreen || reason == PhosphorEngine::RestoreReason::Open;
    const PlacementDirective directive = (ruleMayApply && m_placementZonesResolver)
        ? m_placementZonesResolver(windowId, screenId)
        : PlacementDirective{};
    const SnapResult placementRuleResult =
        ruleMayApply ? calculateSnapToPlacementRule(windowId, screenId, sticky, directive) : SnapResult::noSnap();
    const int openDesktop =
        directive.targetDesktop >= 1 ? directive.targetDesktop : currentVirtualDesktopForScreen(screenId);
    // A matched RouteToDesktop can send the window to a desktop whose context
    // runs a TILING mode while the screen's CURRENT desktop runs snapping, so
    // the mode short-circuit near the top — which asks the current desktop —
    // let it through. The float terminals below pin residence to openDesktop,
    // and writing a snap float verdict (plus a free-size restore) into a
    // context another engine owns is the same visible/state desync that
    // short-circuit exists to prevent. Scoped to the FLOAT terminals: a
    // matched SnapToZone rule names its own zone and is left to the routing
    // it asked for.
    const bool routedIntoForeignMode = m_layoutManager && openDesktop >= 1
        && openDesktop != currentVirtualDesktopForScreen(screenId)
        && m_layoutManager->modeForScreen(screenId, openDesktop, currentActivity())
            != PhosphorZones::AssignmentEntry::Mode::Snapping;
    // The lineage snapshot the size restore gates on, taken before take()
    // can re-bind a FIFO-matched sibling record under this uuid.
    const bool placedBefore = m_windowTracker && placedByPreviousLineage(m_windowTracker->placementStore(), windowId);
    const bool placementRuleWins = placementRuleResult.shouldSnap
        && (!m_shouldRestorePredicate
            || m_shouldRestorePredicate(placementRuleResult.screenId,
                                        placementRuleResult.virtualDesktop >= 1
                                            ? placementRuleResult.virtualDesktop
                                            : currentVirtualDesktopForScreen(placementRuleResult.screenId)));

    // The leave: release every engine slot of the window's own record (snap's
    // and any tiling engine's), silently, so none of them reads as a home to
    // pull the window back to later. Released slots stay as a verdict.
    if (leftScreen) {
        for (auto it = own->engines.constBegin(); it != own->engines.constEnd(); ++it) {
            m_windowTracker->releaseEngineSlot(windowId, it.key());
        }
        qCInfo(PhosphorSnapEngine::lcSnapEngine) << "resolveWindowRestore:" << windowId << "left its recorded screen"
                                                 << own->screenId << "for" << screenId << "— not restored, released";
    }

    // Unified placement store — the single authoritative restore record for this
    // window. Consulted before the legacy "already has assignment" skip and the
    // snap/float chains (but after a matched SnapToZone rule, above). This
    // ordering is load-bearing: the record is the window's authoritative restore
    // answer, and the legacy skip below would otherwise answer first for any
    // window the live stores already hold (a re-resolve within one daemon
    // lifetime: windowOpened after the D-Bus resolve, a DesktopArrival
    // re-drive), so a floated record would never restore floating.
    // Mutual exclusivity (one record per window) means a snapped window never
    // resurrects a stale float (the floated→snapped→login bug). Only snap-owned
    // records on a matching screen are handled here; autotile records are left for
    // autotile's own open path. Falls through to the legacy skip + chain when the
    // store has no record (windows persisted under the old keys before migration).
    // A window that left its screen restores nothing (see leftScreen above).
    if (m_windowTracker && !leftScreen) {
        // The appId FIFO (another instance's record) serves a genuine open and,
        // for a window whose own record no engine ever captured, the two sweeps.
        // A captured own record is final (its slots are the window's verdict,
        // released ones included), and DesktopArrival / Unminimize are re-entries
        // of the window's own record only: they never borrow a sibling's.
        const bool ownIsFinal = own && !own->engines.isEmpty();
        const bool fifoAllowed = reason == PhosphorEngine::RestoreReason::Open
            || ((reason == PhosphorEngine::RestoreReason::DaemonRestartSweep
                 || reason == PhosphorEngine::RestoreReason::PendingSweep)
                && !ownIsFinal);
        const QString appId = fifoAllowed ? m_windowTracker->currentAppIdFor(windowId) : QString();
        // Whether a FLOATED record for THIS window may restore its recorded
        // position on open — the daemon resolves the
        // `snappingRestoreFloatedWindowsOnLogin` setting plus the per-window
        // RestorePosition rule. It governs the geometry MOVE only: the accept
        // predicate below keeps a floated record screen-local whatever this
        // says, so a floated window never crosses monitors on reopen. Snapped
        // records are never governed by this.
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
                // A SNAPPED record restores only where the window opens: on this
                // screen while it snaps, or (another instance's record only) on
                // another virtual screen of the same output that snaps too. A
                // record on another monitor stays for an instance that opens
                // there; a window that opens elsewhere is left where KWin put it.
                // A FLOATED position is screen-local (the gate below).
                if (p.slotFor(engineId()).state == WindowPlacement::stateSnapped()) {
                    return snappedRecordRestorable(p);
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
                // A record no engine ever captured is the pre-tile geometry
                // stub of an open, not a floated placement: consuming it as
                // one would "restore" the spawn frame it carries and skip the
                // free-size restore that exists to undo that frame. The snap
                // open path writes no such stub before resolving today; the
                // gate keeps that an accident of ordering rather than a
                // load-bearing one (takeForReopen carries the same gate).
                if (p.engines.isEmpty()) {
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
                // Among an app's FIFO records, restore a snapped placement this
                // open may restore ahead of an unsnapped (free/floating) sibling
                // that is merely older: snapping is the stronger restore intent.
                // Contentless residue is already excluded by the accept predicate, so
                // the second (merely-accepted) pass only ever sees real placements.
                return p.slotFor(engineId()).state == WindowPlacement::stateSnapped() && snappedRecordRestorable(p);
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

            // The desktop the window is being placed onto, and whether the
            // record snaps it THERE. For a single-desktop record that is the
            // slot's state. A multi-desktop record answers per desktop: the map
            // names a zone for every desktop the window is snapped on, and a
            // desktop it does not name is one the window was unsnapped
            // (floated) on — the forget that maintains the map makes that
            // meaning exact — so slot.state, which only says what the desktop
            // in view at the last capture looked like, cannot decide.
            const int restoreDesktop = restoreDesktopFor(windowId, *rec, restoreScreen);
            // The LIVE activity, on purpose: the window is being restored into
            // the activity the session is in, and the caller's commit is
            // pinned there (stateForWindowOnScreen keys on currentActivity()).
            // Keying the grant and the seeds under the record's activity
            // split one window across two activity keys, and the next
            // membership pass released and forgot the record-keyed half.
            const QString restoreActivity = currentActivity();
            const QStringList restoreZones = slot.zonesByDesktop.isEmpty()
                ? (slot.state == WindowPlacement::stateSnapped() ? slot.zoneIds : QStringList{})
                : slot.zonesByDesktop.value(restoreDesktop);
            if (!restoreZones.isEmpty()) {
                // A stored snap is subject to BOTH the disabled-context gate and
                // the managed-restore gate (restoreWindowsToZonesOnLogin). Either
                // veto falls the window through to the normal auto-snap chain
                // rather than re-applying the recorded zone. The context gate
                // asks about the desktop being restored onto, and runs BEFORE
                // any membership is granted or seeded below, so a refusal
                // leaves nothing behind.
                const bool contextAllows =
                    !m_shouldRestorePredicate || m_shouldRestorePredicate(restoreScreen, restoreDesktop);
                const bool managedAllows = !m_managedRestorePredicate || m_managedRestorePredicate(windowId);
                if (contextAllows && !managedAllows) {
                    // Distinct log so the managed gate (restoreWindowsToZonesOnLogin
                    // off) is identifiable separately from a disabled-context veto.
                    qCDebug(PhosphorSnapEngine::lcSnapEngine)
                        << "resolveWindowRestore:" << windowId
                        << "— managed-restore gate skipped snapped record (restoreWindowsToZonesOnLogin off)";
                }
                // The remembered zones have to belong to the layout the
                // restore context runs NOW. A window snapped into layout A's
                // zone on one desktop and reopened on a desktop running
                // layout B still resolves A's zone geometry (the id is
                // unique across layouts, so the lookup finds it), and the
                // window came back sitting in a layout the desktop no longer
                // has (discussion #1104). Same for a desktop whose layout was
                // switched while the window was closed. Not restorable to a
                // zone here: the chain below places it like a fresh window.
                const bool layoutHoldsZones = PhosphorZones::LayoutUtils::contextLayoutHoldsZones(
                    m_layoutManager, restoreScreen, restoreDesktop, restoreActivity, restoreZones);
                if (contextAllows && managedAllows && !layoutHoldsZones) {
                    qCInfo(PhosphorSnapEngine::lcSnapEngine)
                        << "resolveWindowRestore:" << windowId << "remembered zone(s)" << restoreZones
                        << "are not in the layout for desktop" << restoreDesktop << "of" << restoreScreen
                        << "— not restoring into a layout the context no longer runs";
                }
                if (contextAllows && managedAllows && layoutHoldsZones) {
                    // The restore desktop's own zones; the other desktops' go
                    // back into their own stores below.
                    const QStringList zoneIds = restoreZones;
                    const QRect geo =
                        zoneIds.isEmpty() ? QRect() : m_windowTracker->resolveZoneGeometry(zoneIds, restoreScreen);
                    if (geo.isValid()) {
                        // freeGeo already lives in the record (the single float-back
                        // store) — a later float toggle reads it directly; nothing to
                        // re-seed into a separate per-engine store.
                        // A window this daemon lifetime already committed to these
                        // zones (windowOpened after the D-Bus resolve, a
                        // DesktopArrival re-drive) needs no re-commit, which would
                        // only be a redundant apply: no-op.
                        const SnapState* liveState = stateForWindow(windowId);
                        if (liveState && liveState->isWindowSnapped(windowId)
                            && liveState->zonesForWindow(windowId) == zoneIds) {
                            qCInfo(PhosphorSnapEngine::lcSnapEngine)
                                << "resolveWindowRestore: placement(snapped) already assigned, no-op for" << windowId;
                            return SnapResult::noSnap();
                        }
                        // A multi-desktop record: the restore desktop gets its
                        // membership and the other desktops their zones. The
                        // caller's commit is pinned to restoreDesktop (the
                        // result below carries it), so it writes into THIS
                        // desktop's store whatever the screen is showing.
                        // Granted here, inside the branch that returns a snap,
                        // so a declined restore grants nothing.
                        if (!slot.zonesByDesktop.isEmpty()) {
                            const PhosphorEngine::PlacementStateKey restoreKey{restoreScreen, restoreDesktop,
                                                                               restoreActivity};
                            ensureStateForKey(restoreKey);
                            m_states.addMembership(canonicalWindowId(windowId), restoreKey);
                            seedPersistedDesktopZones(windowId, slot, restoreScreen, restoreDesktop, restoreActivity);
                        }
                        qCInfo(PhosphorSnapEngine::lcSnapEngine) << "resolveWindowRestore: placement(snapped) for"
                                                                 << windowId << "->" << geo << "freeGeo=" << freeGeo;
                        // The desktop is pinned into the result so the commit
                        // stamps the assignment into the store of the desktop
                        // the window is actually on, which the caller's commit
                        // would otherwise read as the screen's current one.
                        return SnapResult{.shouldSnap = true,
                                          .geometry = geo,
                                          .zoneId = zoneIds.first(),
                                          .zoneIds = zoneIds,
                                          .screenId = restoreScreen,
                                          .virtualDesktop = restoreDesktop};
                    }
                }
                // Disabled context, managed-restore opt-out
                // (restoreWindowsToZonesOnLogin off), zone not in the layout
                // the context runs (#1104), or zone gone (layout edit)
                // → fall through to the legacy chain below.
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
                // Pinned to restoreDesktop: the float lives in that desktop's
                // store, not the one the screen happens to show.
                // Read BEFORE the float write below: this branch runs ahead of
                // the already-floating guard, so a re-resolve of the same uuid
                // (a desktop-arrival re-drive; the pending sweep skips tracked
                // windows) lands here again, and the size restore must not
                // fire a second time at a window the user may have sized since.
                const bool alreadyFloating = isFloating(windowId);
                SnapState* restoreState = stateForWindowOnScreen(windowId, restoreScreen, restoreDesktop);
                restoreState->setFloatingOnScreen(windowId, restoreScreen, restoreDesktop);
                // A window floated FROM a snapped state carries its pre-float
                // zones for the resnap path; a never-snapped floated window
                // has none. A multi-desktop record's flat zoneIds are the
                // desktop it was CAPTURED on, so they seed only when that is
                // the desktop being restored onto; restored elsewhere the
                // window had no pre-float zone there before the restart and
                // gets none now (the live path reads the map, which has no
                // entry for it).
                if (!slot.zoneIds.isEmpty()
                    && (slot.zonesByDesktop.isEmpty() || rec->virtualDesktop == restoreDesktop)) {
                    restoreState->addPreFloatZone(windowId, slot.zoneIds);
                    restoreState->addPreFloatScreen(windowId, restoreScreen);
                }
                // Floating on the desktop being restored onto, snapped on
                // others: the other desktops' zones go back into their own
                // stores exactly as a snapped restore's do, or the window
                // comes back floating everywhere.
                seedPersistedDesktopZones(windowId, slot, restoreScreen, restoreDesktop, restoreActivity);
                // The geometry MOVE is gated on the unsnapped-position-restore
                // opt-in (global setting + per-window RestorePosition rule) for
                // ALL floated windows: when off, the window comes back floating
                // but stays where KWin placed it. It is also refused for a
                // recorded rect of a managed size: a window that missed its
                // first size restore closes with the zone-sized frame as its
                // "free" rect, and re-applying it would keep that record alive
                // for every later reopen (the size arm below then finds a real
                // source instead).
                // Key not trusted: this read came from the placement store
                // directly, not through validatedUnmanagedGeometry, so nothing
                // has checked that the rect's coordinates actually describe
                // restoreScreen. A mis-keyed record applied here lands the
                // window on whatever monitor it was really captured on, while
                // the floating-on-screen tracking says restoreScreen — the
                // visible/state desync the comment at the read warns about.
                // Resolved at most ONCE for the two arms below, which ask the
                // same question of the same (screen, desktop). Lazy, so a
                // re-drive that reaches neither arm — an already-floating
                // desktop arrival whose move gate short-circuits — pays no
                // walk at all.
                std::optional<QList<QSize>> managedSizesMemo;
                const auto managedSizes = [&]() -> const QList<QSize>& {
                    if (!managedSizesMemo) {
                        managedSizesMemo = managedSizesOnScreen(restoreScreen, restoreDesktop);
                    }
                    return *managedSizesMemo;
                };
                const bool moveRestored = restoreFloatedPosition && freeGeo.isValid()
                    && (!m_windowTracker || m_windowTracker->geometryBelongsToScreen(freeGeo, restoreScreen))
                    && !isManagedSize(managedSizes(), freeGeo.size());
                if (moveRestored) {
                    Q_EMIT geometryRestoreRequested(windowId, freeGeo, restoreScreen);
                } else if (!alreadyFloating) {
                    // No move, so the window stays where KWin put it, at the
                    // size the app remembers. For a KDE app whose sibling is
                    // snapped that is the zone's size (#1106), exactly as on
                    // the fresh-window terminals below: give the size back
                    // from the record just re-bound above, position untouched.
                    // Straight to the shared arm, reusing the list resolved
                    // above rather than paying restoreFreeSizeForUnplaced's
                    // second walk for the same answer.
                    restoreFreeSizeWhereItStands(m_windowTracker, windowId, restoreScreen, reason, placedBefore,
                                                 managedSizes());
                }
                // The window is floating regardless of whether a position was
                // recorded — tell the compositor (matching toggleWindowFloat /
                // setWindowFloat / handoffReceive), AFTER the geometry or size
                // emit so the effect's float handler sees the reposition
                // already in flight, and only on the transition: a re-resolve
                // of an already-floating window has nothing new to announce.
                if (!alreadyFloating) {
                    Q_EMIT windowFloatingChanged(windowId, true, restoreScreen);
                }
                qCInfo(PhosphorSnapEngine::lcSnapEngine) << "resolveWindowRestore: placement(floated) for" << windowId
                                                         << "->" << freeGeo << "move=" << moveRestored;
                return SnapResult::noSnap();
            }
        }
    }

    // Defensive guard: a window that is already snapped (its WindowPlacement
    // record was normally applied by the store block above, which committed and
    // no-op'd) must not fall through to the auto-snap policy chain and get
    // re-snapped into a different zone. Unreachable in the common path (capture
    // keeps a record for every snapped window), but cheap insurance.
    if (const SnapState* snappedState = stateForWindow(windowId);
        snappedState && snappedState->isWindowSnapped(windowId)) {
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
    // so a placement record saved before the toggle can no longer drag a
    // freshly opened window into a zone the user told us to stay out of.
    // The `isPersistedContextDisabled` filter on disk load fires only once
    // per session, so without this gate a record saved during the running
    // session would leak through. Discussion #461 item 7.
    //
    // Placed AFTER the isWindowSnapped/consume guard so windows that are
    // already snapped still consume their appId pending entry; placed
    // BEFORE the exclusion lookup and the calculate* chain so placement rules,
    // session restore, empty-zone, and last-zone fallbacks are all gated
    // by the same predicate.
    //
    // Predicate is daemon-injected; absence means "no gating" — the
    // historical default that unit tests rely on.
    if (m_shouldRestorePredicate && !m_shouldRestorePredicate(screenId, currentVirtualDesktopForScreen(screenId))) {
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
    // tracker. isWindowExcluded resolves the full WindowQuery (class/title/
    // frame size) through the unified RuleEvaluator and applies the
    // minimum-window-size thresholds — the same match model the autotile
    // engine uses, replacing the hand-rolled appIdMatches loops.
    // The opening screen is passed as the query's screen hint: the window is not
    // in a SnapState yet, so the daemon cannot resolve its screen on its own and
    // an Exclude rule keyed on ScreenId / ActiveLayout would not resolve here.
    if (m_windowTracker && isWindowExcluded(windowId, screenId)) {
        qCInfo(PhosphorSnapEngine::lcSnapEngine) << "resolveWindowRestore:" << windowId << "excluded by rule or size";
        return SnapResult::noSnap();
    }

    // Floating windows are never auto-snapped — skip the chain. (A skip guard, not
    // a fallback level.) No OSD feedback here: this is the automatic window-open /
    // restore path, not an interactive float toggle — the user did not act, so a
    // "floated" toast would be spurious (and would spam at login, where the
    // retry net re-resolves every floating window, including the now-default
    // floated ones). Interactive feedback lives in toggleWindowFloat.
    if (isFloating(windowId)) {
        qCInfo(PhosphorSnapEngine::lcSnapEngine)
            << "resolveWindowRestore: window" << windowId << "is floating, skipping snap";
        return SnapResult::noSnap();
    }

    // A matched "Float this app" rule opens the window floating, exactly
    // as if the user had toggled float. Runs after the exclusion gate (excluded
    // windows return earlier), the already-floating guard, and the persisted-
    // placement restore (a window the user previously snapped reopens snapped —
    // that restore returns before reaching here), but before the auto-snap chain —
    // so a rule-floated window never auto-snaps to a zone.
    // Mirrors the no-match default-float terminal at the end of this function.
    if (!routedIntoForeignMode && m_floatPredicate && m_floatPredicate(windowId, screenId)) {
        // Residence pinned to the routed open desktop (see openDesktop), size
        // emit ahead of the float emit (see the header comment).
        stateForWindowOnScreen(windowId, screenId, openDesktop)->setFloatingOnScreen(windowId, screenId, openDesktop);
        restoreFreeSizeForUnplaced(windowId, screenId, openDesktop, reason, placedBefore);
        Q_EMIT windowFloatingChanged(windowId, true, screenId);
        qCInfo(PhosphorSnapEngine::lcSnapEngine) << "resolveWindowRestore:" << windowId << "floated by rule";
        return SnapResult::noSnap();
    }

    // (SnapToZone placement rules are resolved BEFORE the placement store, near
    // the top of this function — an explicit rule outranks any remembered
    // placement. See the "highest-priority restore" block above.)

    // (Persisted session restore is now served entirely by the unified
    // WindowPlacementStore block at the top of this function — a snapped window
    // reopens from its WindowPlacement record. It is no longer a chain level.)

    // Levels 2 and 3 inherently target the caller's screen (the empty-zone /
    // last-zone lookups are scoped to screenId, not to a saved zone). If the
    // caller's screen is now in a tiling mode, skip them — stale snap zones
    // on a tiled screen must not be auto-assigned, the tiling engine owns
    // placement there.
    if (m_layoutManager) {
        const int dt = currentVirtualDesktopForScreen(screenId);
        if (m_layoutManager->modeForScreen(screenId, dt, currentActivity())
            != PhosphorZones::AssignmentEntry::Mode::Snapping) {
            qCDebug(PhosphorSnapEngine::lcSnapEngine)
                << "resolveWindowRestore:" << windowId << "caller screen" << screenId
                << "is non-snap-mode — skipping empty/last zone fallbacks";
            return SnapResult::noSnap();
        }
    }

    // A window that left its screen takes the float default below, in place:
    // the auto-snap levels would put it into a zone the user never chose.
    // 2. Auto-assign to empty zone
    if (!leftScreen) {
        SnapResult result = calculateSnapToEmptyZone(windowId, screenId, sticky);
        if (result.shouldSnap) {
            qCInfo(PhosphorSnapEngine::lcSnapEngine)
                << "resolveWindowRestore: emptyZone matched for" << windowId << "zone=" << result.zoneId;
            return result;
        }
    }

    // 3. Snap to last zone (final fallback)
    if (!leftScreen) {
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
    // paths treat it like a tiling engine's floated windows. Reached ONLY here: the
    // non-snap-mode defer, disabled-context, exclusion, already-floating and
    // already-snapped guards above all return earlier, and the non-snap-caller
    // short-circuit returns before the empty/last-zone chain — so this is always a
    // genuine snap-mode window with no zone match.
    //
    // The mode short-circuit above asks the SCREEN'S CURRENT desktop, so the
    // routed case needs its own answer, which routedIntoForeignMode supplies: a
    // window a RouteToDesktop sent onto a tiling-mode desktop is that engine's,
    // and gets no snap float residence.
    if (routedIntoForeignMode) {
        qCInfo(PhosphorSnapEngine::lcSnapEngine)
            << "resolveWindowRestore:" << windowId << "routed onto desktop" << openDesktop
            << "which runs a tiling mode — leaving it to that engine";
        return SnapResult::noSnap();
    }
    stateForWindowOnScreen(windowId, screenId, openDesktop)->setFloatingOnScreen(windowId, screenId, openDesktop);
    // Floating where KWin put it, but at the size the app remembers, which
    // for an app with a snapped window is the zone's. Give it its free size,
    // ahead of the float emit (see the header comment).
    restoreFreeSizeForUnplaced(windowId, screenId, openDesktop, reason, placedBefore);
    Q_EMIT windowFloatingChanged(windowId, true, screenId);
    qCInfo(PhosphorSnapEngine::lcSnapEngine)
        << "resolveWindowRestore:" << windowId << "no snap match — defaulting to floated on" << screenId;
    return SnapResult::noSnap();
}

int SnapEngine::currentVirtualDesktop() const
{
    return m_virtualDesktopManager ? m_virtualDesktopManager->currentDesktop() : 0;
}

int SnapEngine::currentVirtualDesktopForScreen(const QString& screenId) const
{
    // Per-output virtual desktops (#648): a snapped window's context binds to the
    // desktop of the screen it lives on, not the global current desktop.
    return m_virtualDesktopManager ? m_virtualDesktopManager->currentDesktopForScreen(screenId) : 0;
}

QString SnapEngine::currentActivity() const
{
    return m_layoutManager ? m_layoutManager->currentActivity() : QString();
}

bool SnapEngine::isSnapModeScreen(const QString& screenId) const
{
    // Permissive without a layout manager, matching resolveWindowRestore's
    // ownership gate (the unit-test path).
    return !m_layoutManager
        || m_layoutManager->modeForScreen(screenId, currentVirtualDesktopForScreen(screenId), currentActivity())
        == PhosphorZones::AssignmentEntry::Mode::Snapping;
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
    return capturePlacementAtDesktop(windowId, 0);
}

std::optional<PhosphorEngine::WindowPlacement> SnapEngine::capturePlacementAtDesktop(const QString& windowId,
                                                                                     int gateDesktop) const
{
    using PhosphorEngine::WindowPlacement;
    if (windowId.isEmpty() || !m_globals) {
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
    // Resolve the window's own screen once: the mode gate below and the
    // captured per-output desktop (#648) both key on it. A window present on
    // several desktops and adopted into the one in view without being
    // snapped there has no residence in its primary store; its screen is the
    // one every member store names, so it is borrowed from the first.
    QString effScreen = screenForTrackedWindow(windowId);
    if (effScreen.isEmpty()) {
        for (const PhosphorEngine::PlacementStateKey& key :
             m_states.membershipsForWindow(canonicalWindowId(windowId))) {
            if (!key.screenId.isEmpty()) {
                effScreen = key.screenId;
                break;
            }
        }
    }
    if (!effScreen.isEmpty()) {
        // Prefer the injected LIVE resolver (see setLiveModeResolver): a
        // screen ENTERING a tiling mode has the cascade flipped before any
        // engine claims it, and the pre-flip presave must still capture
        // its live snap state — the raw cascade would refuse here and the
        // float/zone restore on return to snapping would have nothing to
        // read. Once the tiling engine claims the screen the resolver
        // reports the tiling mode and the frozen-memory refusal applies.
        if (gateDesktop >= 1 && m_layoutManager) {
            // Explicit-desktop gate (the cross-desktop handoff): the question
            // is whether the DESTINATION context is snapping, which the live
            // resolver cannot answer — its signature is screen-only, so it
            // reports the visible desktop's mode.
            if (m_layoutManager->modeForScreen(effScreen, gateDesktop, currentActivity())
                != PhosphorZones::AssignmentEntry::Mode::Snapping) {
                return std::nullopt;
            }
        } else if (m_liveModeResolver) {
            if (m_liveModeResolver(effScreen) != PhosphorZones::AssignmentEntry::Mode::Snapping) {
                return std::nullopt;
            }
        } else if (m_layoutManager
                   && m_layoutManager->modeForScreen(effScreen, currentVirtualDesktopForScreen(effScreen),
                                                     currentActivity())
                       != PhosphorZones::AssignmentEntry::Mode::Snapping) {
            return std::nullopt;
        }
    }

    WindowPlacement p;
    p.windowId = windowId;
    p.appId = m_windowTracker ? m_windowTracker->currentAppIdFor(windowId) : QString();
    // Bind the captured desktop to the window's OWN screen, not the global current
    // (Plasma 6.7 per-output virtual desktops, #648), so a float-back restores to
    // the right desktop on a screen that isn't the active one. This is only the
    // FALLBACK: the branches below prefer the store's RECORDED desktop, because
    // the screen's current desktop is not the window's — a window snapped or
    // floated on desktop 2 must not have its record rewritten to desktop 1 by a
    // refresh capture that happens to run after the user switched away.
    p.virtualDesktop = currentVirtualDesktopForScreen(effScreen);
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
    const SnapState* state = stateForWindow(windowId);
    // A window present on several desktops is snapped separately on each,
    // and zoneIds below can only carry one of those. Gather the rest so a
    // restart puts it back in the zone it occupied on EVERY desktop instead
    // of one zone everywhere. Written only for a genuinely multi-desktop
    // window, so an ordinary record is unchanged. Keyed by the canonical id,
    // which is what the membership map is keyed by; the raw id of a
    // class-mutated window would find no memberships and write nothing.
    // Gathered for a FLOATING window too: floating is per store, so a window
    // floated on the desktop in view is still snapped on the others, and a
    // record that dropped those zones would restore it floating everywhere.
    const auto gatherZonesByDesktop = [this, &windowId, &slot]() {
        const QList<PhosphorEngine::PlacementStateKey> memberships =
            m_states.membershipsForWindow(canonicalWindowId(windowId));
        if (memberships.size() <= 1) {
            return;
        }
        for (const PhosphorEngine::PlacementStateKey& key : memberships) {
            const SnapState* perDesktop = m_states.stateForKey(key);
            if (!perDesktop || key.desktop < 1) {
                continue;
            }
            const QStringList zones = perDesktop->zonesForWindow(windowId);
            if (!zones.isEmpty()) {
                slot.zonesByDesktop.insert(key.desktop, zones);
            }
        }
    };
    if (isFloating(windowId)) {
        slot.state = WindowPlacement::stateFloating();
        slot.zoneIds = state ? state->preFloatZones(windowId) : QStringList{};
        // The same borrow the unsnapped-member branch below uses: a floating
        // multi-desktop window with no residence in its primary store still
        // has a screen, the one its member stores name.
        p.screenId = effScreen;
        // The RECORDED desktop wins over the screen's current one — a plain
        // setFloating (globals store) records none, hence the >= 1 guard.
        if (const int recorded = state ? state->desktopForWindow(windowId) : 0; recorded >= 1) {
            p.virtualDesktop = recorded;
        }
        gatherZonesByDesktop();
    } else if (state && state->isWindowSnapped(windowId)) {
        slot.state = WindowPlacement::stateSnapped();
        slot.zoneIds = state->zonesForWindow(windowId);
        p.screenId = state->screenForWindow(windowId);
        // Same recorded-desktop preference as the floating branch.
        if (const int recorded = state->desktopForWindow(windowId); recorded >= 1) {
            p.virtualDesktop = recorded;
        }
        gatherZonesByDesktop();
    } else {
        // Snapping has only two states — snapped (above) or floated. An unmanaged
        // window on a snap-mode screen is FLOATED (the retired `free` state). The
        // orchestrator fills freeGeometryByScreen from the live frame; a contentless
        // capture (no frame) is dropped by hasRestorableContent() so geometry-less
        // floated residue never floods the per-app FIFO.
        //
        // But a window this engine does not track AT ALL is a different case: snap
        // has NO knowledge of it — typically because a cross-engine handoffRelease
        // just handed it to autotile, leaving the record's snap slot as the FROZEN
        // per-mode memory that windowsReleased restores from on return to snapping.
        // Fabricating a floated slot here overwrote that frozen memory (a window
        // snapped in snap mode, mode-toggled through autotile, came back "floating"
        // — the phantom snap-float restore). Return nullopt instead: the capture
        // orchestrator's no-engine contract leaves the existing record intact, and
        // a genuinely untracked window's close still persists via the
        // recordFloatingClose fallback.
        if (!isWindowTracked(windowId)) {
            return std::nullopt;
        }
        slot.state = WindowPlacement::stateFloating();
        // A window present on several desktops, unsnapped on the one in view
        // (an adopted membership with no data), records as floating HERE with
        // its other desktops' zones kept: snapping's two states make unsnapped
        // and floated the same thing, and the restore decides per desktop
        // from the map. Its screen is the member-store borrow resolved above.
        p.screenId = effScreen;
        gatherZonesByDesktop();
    }
    p.engines.insert(engineId(), slot);
    return p;
}

} // namespace PhosphorSnapEngine
