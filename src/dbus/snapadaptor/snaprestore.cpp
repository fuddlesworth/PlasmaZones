// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "snapadaptor.h"
#include "dbus/windowtrackingadaptor/windowtrackingadaptor.h"
#include "core/interfaces/interfaces.h"
#include "core/platform/logging.h"
#include <PhosphorPlacement/WindowTrackingService.h>
#include <PhosphorScreens/Manager.h>
#include "core/interfaces/isettings.h"
#include <PhosphorContext/ContextResolver.h>
#include <PhosphorSnapEngine/SnapEngine.h>

#include <QScopeGuard>

namespace PlasmaZones {

namespace {
// Non-blocking startup gate shared by all synchronous snap D-Bus methods.
//
// Rationale: these slots return zone geometry synchronously to the KWin effect.
// Before the first panel D-Bus query completes, PhosphorScreens::ScreenManager's availability cache
// is empty and zones would be computed against the unreserved full-screen rect —
// handing the effect coordinates that place the window partially behind the panel.
//
// The effect already waits for WindowTrackingAdaptor::pendingRestoresAvailable before
// issuing initial restore calls, and that signal is gated on panelGeometryReady (see
// tryEmitPendingRestoresAvailable in persistence.cpp). This helper is belt-and-suspenders:
// if a snap slot is nevertheless invoked before panel geometry is known — a bug in the
// effect-side ordering, a programmatic D-Bus client, or a future refactor — we log once
// per session and return shouldSnap=false rather than handing back wrong coordinates.
// The effect treats shouldSnap=false as "no snap" and leaves the window where KWin placed
// it, which is the same fallback as if the slot had never been called.
// @p warned is the CALLER's latch, not a function-local static: a static here
// is process-wide, so in a ctest binary the first fixture to hit this path
// would swallow the warning for every fixture after it.
bool isSnapReadyOrWarn(PhosphorPlacement::WindowTrackingService* service, const char* method, bool& warned)
{
    auto* mgr = service ? service->screenManager() : nullptr;
    if (!mgr || mgr->isPanelGeometryReady()) {
        return true;
    }
    if (!warned) {
        warned = true;
        qCWarning(lcDbusWindow) << method << "called before panel geometry ready — returning no-snap."
                                << "The KWin effect should gate restore calls on pendingRestoresAvailable;"
                                << "if you see this, the effect-side gate was bypassed or is racing startup.";
    } else {
        qCDebug(lcDbusWindow) << method << "called before panel geometry ready — returning no-snap";
    }
    return false;
}
} // namespace

void SnapAdaptor::snapToLastZone(const QString& windowId, const QString& windowScreenId, bool sticky, int& snapX,
                                 int& snapY, int& snapWidth, int& snapHeight, bool& shouldSnap)
{
    snapX = snapY = snapWidth = snapHeight = 0;
    shouldSnap = false;

    // Empty windowId or screen is a precondition violation that the sibling
    // slots (snapToAppRule, snapToEmptyZone, resolveWindowRestore) all guard;
    // mirror their early-return so the input contract is symmetric across
    // the snap-restore family. The calculators resolve the layout, the
    // last-used state and the desktop filter from the screen, so an empty one
    // has no honest answer to give.
    if (windowId.isEmpty() || windowScreenId.isEmpty()) {
        return;
    }

    if (!m_adaptor || !m_adaptor->service()) {
        return;
    }

    if (!isSnapReadyOrWarn(m_adaptor->service(), "snapToLastZone", m_snapNotReadyWarned)) {
        return;
    }

    if (!m_engine) {
        return;
    }

    SnapResult result = m_engine->calculateSnapToLastZone(windowId, windowScreenId, sticky);
    if (!result.shouldSnap) {
        return;
    }

    if (!applySnapResult(result, windowId, snapX, snapY, snapWidth, snapHeight, shouldSnap)) {
        return;
    }
    qCInfo(lcDbusWindow) << "Snapping new window" << windowId << "to last used zone" << result.zoneId;
}

void SnapAdaptor::snapToAppRule(const QString& windowId, const QString& windowScreenName, bool sticky, int& snapX,
                                int& snapY, int& snapWidth, int& snapHeight, bool& shouldSnap)
{
    snapX = snapY = snapWidth = snapHeight = 0;
    shouldSnap = false;

    if (windowId.isEmpty() || windowScreenName.isEmpty()) {
        return;
    }

    if (!m_adaptor || !m_adaptor->service()) {
        return;
    }

    if (!isSnapReadyOrWarn(m_adaptor->service(), "snapToAppRule", m_snapNotReadyWarned)) {
        return;
    }

    if (!m_engine) {
        return;
    }

    SnapResult result = m_engine->calculateSnapToPlacementRule(windowId, windowScreenName, sticky);
    if (!result.shouldSnap) {
        return;
    }

    if (!applySnapResult(result, windowId, snapX, snapY, snapWidth, snapHeight, shouldSnap)) {
        return;
    }
    qCInfo(lcDbusWindow) << "Placement rule snapping window" << windowId << "to zone" << result.zoneId;
}

void SnapAdaptor::snapToEmptyZone(const QString& windowId, const QString& windowScreenId, bool sticky, int& snapX,
                                  int& snapY, int& snapWidth, int& snapHeight, bool& shouldSnap)
{
    snapX = snapY = snapWidth = snapHeight = 0;
    shouldSnap = false;

    if (windowId.isEmpty() || windowScreenId.isEmpty()) {
        return;
    }

    if (!m_adaptor || !m_adaptor->service()) {
        return;
    }

    if (!isSnapReadyOrWarn(m_adaptor->service(), "snapToEmptyZone", m_snapNotReadyWarned)) {
        return;
    }

    if (!m_engine) {
        return;
    }

    qCDebug(lcDbusWindow) << "snapToEmptyZone: windowId=" << windowId << "screen=" << windowScreenId;
    SnapResult result = m_engine->calculateSnapToEmptyZone(windowId, windowScreenId, sticky);
    if (!result.shouldSnap) {
        qCDebug(lcDbusWindow) << "snapToEmptyZone: no snap";
        return;
    }

    if (!applySnapResult(result, windowId, snapX, snapY, snapWidth, snapHeight, shouldSnap)) {
        return;
    }
    qCInfo(lcDbusWindow) << "Auto-assign snapping window" << windowId << "to empty zone" << result.zoneId;
}

// restoreToPersistedZone removed — session zone restoration is served by the
// unified WindowPlacementStore via resolveWindowRestore. The old D-Bus slot had
// no remaining caller (the effect uses resolveWindowRestore).

void SnapAdaptor::resolveWindowRestore(const QString& windowId, const QString& screenId, bool sticky, int windowKind,
                                       int restoreReason, int minWidth, int minHeight, int& snapX, int& snapY,
                                       int& snapWidth, int& snapHeight, bool& shouldSnap)
{
    snapX = snapY = snapWidth = snapHeight = 0;
    shouldSnap = false;

    if (windowId.isEmpty() || screenId.isEmpty()) {
        return;
    }

    if (!m_engine) {
        qCWarning(lcDbusWindow) << "resolveWindowRestore: no SnapEngine available";
        return;
    }

    if (!m_adaptor || !m_adaptor->service()) {
        return;
    }

    if (!isSnapReadyOrWarn(m_adaptor->service(), "resolveWindowRestore", m_snapNotReadyWarned)) {
        return;
    }

    const PhosphorEngine::RestoreReason reason = PhosphorEngine::clampRestoreReasonFromWire(restoreReason);
    const bool isOpen = reason == PhosphorEngine::RestoreReason::Open;
    auto* const svc = m_adaptor->service(); // non-null: guarded above

    // Claim this instance's placement record BEFORE anything reads one. At
    // login every uuid is fresh, so every selector over this appId bucket falls
    // to the same pool of records. Claiming once pins WHICH record belongs to
    // this instance for the whole open and for the re-drives that follow it:
    // without it an already-home window could consume a sibling's record
    // outright, and a multi-window app's later re-resolve could answer from a
    // different record than the one its open used.
    //
    // Open AND PendingSweep. The sweep re-resolves windows that are already
    // OPEN, but for the placement store it CAN still be a first touch: a window
    // whose open resolve arrived before the daemon was ready never got past the
    // readiness gate above, so it never claimed anything. For one that did
    // already claim, claimForOpen is idempotent and this is harmless. Without this the pairing guard was
    // absent in exactly the slow-daemon login the feature exists for.
    //
    // The other drivers are true re-entries (Unminimize, DesktopArrival) or run
    // with stable uuids that route to the store's same-instance branch
    // (DaemonRestartSweep), so their claim or consumption is already settled.
    if (isOpen || reason == PhosphorEngine::RestoreReason::PendingSweep) {
        svc->placementStore().claimForOpen(windowId, svc->currentAppIdFor(windowId));
    }

    // Engine-neutral RouteToDesktop runs first and unconditionally — a window can
    // be routed to a desktop whether or not it snaps (and even when it doesn't
    // match a SnapToZone rule at all), so it must not sit behind the shouldSnap
    // early-return below.
    m_adaptor->applyOpenDesktopRouting(windowId, screenId);
    // Whatever that wrote belongs to THIS open and nothing later. The placement
    // builders take the record when they run, but not every path reaches one:
    // a scrolling or tiling screen defers by mode, and a window carrying a
    // cross-screen snapped record can report a snap without the zone builder
    // running at all. A guard rather than a call at each exit, because this
    // slot has several and the reader it would leak to (unfloatToZone, via the
    // same placement builder) fires long after, on a Meta+F.
    const auto routeRecordGuard = qScopeGuard([this, &windowId] {
        m_adaptor->clearOpenWorkspaceRoute(windowId);
    });

    const PhosphorEngine::WindowKind kind = PhosphorEngine::clampWindowKindFromWire(windowKind);
    SnapResult result = m_engine->resolveWindowRestore(windowId, screenId, sticky, kind);

    // Per-open reclaim-credit burn, the snap-screen half of the partition
    // (WindowPlacementStore::burnReclaimCredit documents the tiling half —
    // takeForReopen, which snap never calls). Runs for genuine OPENS on
    // SNAP-mode screens only: tiling-screen arrivals burn through their
    // engine's own open path, and the re-resolve drivers of this slot
    // (unminimize, the sweeps, the desktop-arrival re-drive) must retire
    // nothing. Skipped when a tiling claim adopts the window below — the
    // adopted windowOpened's takeForReopen is that open's burn.
    //
    // WHY DesktopArrival DOES NOT BURN. burnReclaimCredit is not idempotent:
    // each call retires the newest eligible SIBLING's credit, so two calls
    // for one logical open spend two siblings' credits and strand a window
    // that has not reopened yet. This slot now always reaches the burn on
    // the open pass — a window a RouteToDesktop rule sends to another
    // desktop is parked by the effect and re-enters here with
    // DesktopArrival, which would be that second call. The arrival is the
    // continuation of an open that already burned, never an open of its own.
    // The other producer, the move-to-desktop shortcut, acts on an
    // already-open window and must retire nothing either.
    bool reclaimedByTiling = false;
    const auto burnOpenCredit = [&]() {
        if (!isOpen || reclaimedByTiling || !m_engine->isSnapModeScreen(screenId)) {
            return;
        }
        const QString appId = svc->currentAppIdFor(windowId);
        if (PhosphorEngine::hasStableAppIdFor(appId, windowId)) {
            svc->placementStore().burnReclaimCredit(windowId, appId);
        }
    };

    if (!result.shouldSnap) {
        // Nothing snapped this window. A bare RouteToScreen rule (move-to-monitor
        // with no SnapToZone) takes effect here, deliberately AFTER the snap/float
        // restore has had its chance: a SnapToZone restore or a remembered snap
        // already returned shouldSnap=true above (so the route never fights a snap),
        // and the explicit route wins over a remembered float position AND over
        // the cross-screen reclaim below (it applies the final geometry). A
        // route WITH SnapToZone moved+snapped on the target via the placement
        // directive and never reaches here.
        const bool routed = m_adaptor->applyOpenScreenRouting(windowId, screenId);
        // Cross-screen tiling-engine reclaim — gated on the ENGINE's explicit
        // defer verdict, never on a bare no-snap: an exclusion refusal, a
        // disabled context, or an ordinary no-match must not hand the window
        // to a reclaim those gates already settled. This is the channel that
        // covers arrivals on SNAP-mode screens, which the tiling dispatch
        // never hears about — the engine whose TILED slot the record carries
        // adopts the window into its recorded home and its retile moves it
        // there. (Managed-screen arrivals reach the reclaim through
        // TilingAdaptor::dispatchOpenToClaimingEngine instead; windows that
        // fail the effect's canSnapRestore gate never reach this slot at
        // all — see setCrossScreenTileReclaim's contract.) The reason gate
        // keeps the drivers that re-resolve an ALREADY-VISIBLE window (the
        // unminimize of a daemon-restart orphan, the pending-restores sweep,
        // the bring-up stacking sweep) from teleporting a window the user is
        // looking at.
        //
        // DesktopArrival is admitted alongside Open, and that is the whole
        // reason this argument stopped being a bool. It is a CONTINUATION of
        // an open, not a user action: a RouteToDesktop rule sent this window to
        // another desktop at open time, the effect parked it while that desktop
        // was not showing, and this is the re-drive once it landed. The open
        // pass deliberately placed nothing for it. Denying it the reclaim —
        // which the bool did, since it is not an open — leaves any window whose
        // record was tiled on another screen on the no-match float default for
        // the rest of the session.
        //
        // Note this admits the reclaim but NOT the reclaim-credit burn: see
        // burnOpenCredit above, where the open pass already spent this open's
        // one credit. Reclaiming reads a credit, burning retires a sibling's.
        //
        // This DOES relax the never-move-a-visible-window rule, deliberately.
        // The effect only drains an arrival once the window's own output is
        // showing its desktop, so a reclaimed window is on screen at that
        // moment. The cost is the delayed case: a park held until the user
        // first visits that desktop much later, where the reclaim lands as a
        // visible jump. That is the better trade against stranding the window
        // on the wrong monitor for the whole session.
        const bool mayReclaim = isOpen || reason == PhosphorEngine::RestoreReason::DesktopArrival;
        if (result.deferredToTilingEngine && !routed) {
            const bool reclaimed = mayReclaim && m_crossScreenTileReclaim
                && m_crossScreenTileReclaim(windowId, screenId, qMax(0, minWidth), qMax(0, minHeight));
            reclaimedByTiling = reclaimed;
            if (!reclaimed) {
                // Non-open, or DECLINED (the claims ask stricter questions —
                // live sets, context equality, tileability — than the
                // defer): the engine's defer skipped its float terminal on
                // the promise someone would manage the window, so restore
                // the no-match float default rather than leaving it with no
                // state in any engine.
                m_engine->applyNoMatchFloatDefault(windowId, screenId);
            }
        }
        // A matched route is deliberately NOT followed by the float default:
        // the route already applied final geometry on its TARGET screen, and
        // writing float state here would record the SPAWN screen — the one
        // the window is leaving. The route owns the placement, which is what
        // the defer's "someone will manage it" promise needed.
        burnOpenCredit();
        return;
    }

    applySnapResult(result, windowId, snapX, snapY, snapWidth, snapHeight, shouldSnap);
    // Return value intentionally ignored: applySnapResult has already set
    // shouldSnap (false on a disabled-context refusal) and there is no
    // post-snap work in this slot to skip.
    //
    // In particular, a refusal here does NOT fall through to
    // applyOpenScreenRouting. That asymmetry is deliberate. Both of
    // applySnapResult's refusals mean the user has told us to keep our hands off
    // this window — snapping is globally disabled, or the destination context is
    // marked disabled — so honouring the rule's RouteToScreen and moving the
    // window anyway would act on exactly the context that just said no. The
    // RouteToDesktop above is different: it is emitted before the engine is
    // consulted at all, by design, because a desktop route is independent of
    // whether the window snaps.
    burnOpenCredit();
}

bool SnapAdaptor::applySnapResult(const SnapResult& result, const QString& windowId, int& snapX, int& snapY,
                                  int& snapWidth, int& snapHeight, bool& shouldSnap)
{
    snapX = snapY = snapWidth = snapHeight = 0;
    shouldSnap = false;

    if (!m_adaptor || !m_adaptor->service() || !m_engine) {
        return false;
    }

    // Global snapping kill-switch — see discussion #461 item 2. Every snapTo*
    // / resolveWindowRestore D-Bus slot funnels
    // through here, so a single gate suppresses all auto-snap-on-open paths
    // when the user has turned snapping off entirely. Mirrors the
    // engine-internal gate in SnapEngine::resolveWindowRestore.
    if (m_settings && !m_settings->snappingEnabled()) {
        qCInfo(lcDbusWindow) << "applySnapResult: refusing auto-snap of" << windowId
                             << "— snapping is globally disabled";
        return false;
    }

    // Disabled-context gate. The interactive drag path (WindowDragAdaptor)
    // and autotile (Daemon::updateEngineScreens) already refuse to place
    // windows on a monitor / desktop / activity the user marked disabled.
    // The auto-snap-on-open restore path — every snapTo* / resolveWindowRestore
    // slot funnels through here — did not, so windows still snapped on a
    // disabled context (discussion #461). Gating here covers all restore
    // entry points in one place.
    if (m_settings && !result.screenId.isEmpty()) {
        // Gate against the DESTINATION screen's actual mode. A restore result
        // can cross-screen-migrate (placement rule / session restore) onto a screen
        // whose mode differs from the caller's, so the disable list to consult
        // is the one for result.screenId's mode — not a hard-coded Snapping.
        //
        // The destination DESKTOP is result.virtualDesktop when the result was
        // routed there (a RouteToDesktop placement, calculateSnapToPlacementRule),
        // otherwise the current desktop (every other calculator opens the window on
        // the current desktop, or refuses outright on a saved-desktop mismatch).
        // For a routed result handleForPersisted composes the explicit destination
        // desktop, so the disable check keys on the desktop the window will actually
        // land on rather than the live current desktop; for a non-routed result
        // (virtualDesktop == 0) handleFor is exact, pulling (currentVirtualDesktop,
        // currentActivity) from the daemon's VDM/AM — the same values the snap engine
        // sees — and routing the screen through the mode provider in one snapshot.
        if (m_contextResolver) {
            const auto handle = result.virtualDesktop >= 1
                ? m_contextResolver->handleForPersisted(result.screenId, result.virtualDesktop,
                                                        m_contextResolver->currentActivity())
                : m_contextResolver->handleFor(result.screenId);
            if (m_contextResolver->isDisabled(handle)) {
                qCInfo(lcDbusWindow) << "applySnapResult: refusing auto-snap of" << windowId
                                     << "— PlasmaZones is disabled for screen" << result.screenId;
                return false;
            }
        }
    }

    // A shouldSnap result can carry an empty zoneId; committing it would
    // record a snap to no zone (the drop path refuses the same shape).
    // Refused BEFORE any out-param write or side effect: the callers reply
    // over D-Bus with whatever landed in the out-params, so writing the
    // geometry (or marking auto-snapped) first would ship shouldSnap=true
    // for a snap that never committed — the applySnapResult doc's
    // "leaves the out-params at 0 / false" contract.
    const QStringList zoneIds = result.zoneIds.isEmpty() ? QStringList{result.zoneId} : result.zoneIds;
    if (zoneIds.first().isEmpty()) {
        qCWarning(lcDbusWindow) << "shouldSnap resolved an empty zone id for" << windowId << "- skipping";
        return false;
    }

    snapX = result.geometry.x();
    snapY = result.geometry.y();
    snapWidth = result.geometry.width();
    snapHeight = result.geometry.height();
    shouldSnap = true;

    // Mark auto-snapped first so the flag persists through commitSnap
    // (AutoRestored leaves it alone). commitSnap runs the full
    // orchestration — clears any pre-existing floating state (emits
    // windowFloatingClearedForSnap which the adaptor relays as
    // windowFloatingChanged), assigns to zone(s), emits state change.
    m_adaptor->service()->markAsAutoSnapped(windowId);
    if (zoneIds.size() > 1) {
        m_engine->commitMultiZoneSnap(windowId, zoneIds, result.screenId, SnapIntent::AutoRestored,
                                      result.virtualDesktop);
    } else {
        m_engine->commitSnap(windowId, zoneIds.first(), result.screenId, SnapIntent::AutoRestored,
                             result.virtualDesktop);
    }
    // Focus-new-windows is handled inside SnapEngine::commitSnapImpl on the
    // AutoRestored path (mirrors AutotileEngine), so it covers every auto-snap-on-open
    // entry point in one place — not just this D-Bus facade.
    return true;
}

} // namespace PlasmaZones
