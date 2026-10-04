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
#include <PhosphorSnapEngine/SnapState.h>

#include <utility>

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
    // Pinned by the API v9 wire signature, accepted and ignored: they sized a
    // cross-screen tiling reclaim, which the reopen contract removed.
    Q_UNUSED(minWidth)
    Q_UNUSED(minHeight)

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

    // A window arriving on a desktop it is already snapped on answers with its
    // zone's rect: its client was suspended there, so the resize that came with
    // the move was never acked and the compositor will not re-send it. The
    // assignment already exists, so the out-params are written here rather than
    // through applySnapResult, which would re-run the whole snap orchestration;
    // snapPermittedForContext asks its two user-facing refusals by hand. Snap
    // screens only (layoutForScreen also answers for a tiling context). The zone
    // is read from the ARRIVAL CONTEXT'S OWN store, not SnapEngine::zoneForWindow,
    // whose primary membership can name another desktop's zone: that is the
    // ghost of discussion #1104 by a new route. A floated window keeps its zone
    // as the memory a float toggle resnaps into, so it is left floating.
    const auto answerHeldZone = [&]() {
        if (!m_engine->isSnapModeScreen(screenId)
            || !snapPermittedForContext(windowId, screenId, /*virtualDesktop=*/0)) {
            return false;
        }
        const auto* const contextState =
            static_cast<const PhosphorSnapEngine::SnapState*>(std::as_const(*m_engine).stateForScreen(screenId));
        const QString heldZone =
            (contextState && !contextState->isFloating(windowId)) ? contextState->zoneForWindow(windowId) : QString();
        const QRect heldGeometry = heldZone.isEmpty() ? QRect() : svc->zoneGeometry(heldZone, screenId);
        if (!heldGeometry.isValid()) {
            return false;
        }
        snapX = heldGeometry.x();
        snapY = heldGeometry.y();
        snapWidth = heldGeometry.width();
        snapHeight = heldGeometry.height();
        shouldSnap = true;
        qCInfo(lcDbusWindow) << "resolveWindowRestore: desktop arrival re-applies" << windowId << "to zone" << heldZone
                             << heldGeometry;
        return true;
    };
    // A move that was not an open's continuation (the desktop shortcut, a
    // cross-mode handoff, the shell) re-applies the zone the window holds where
    // it landed and nothing else: no claim, no routing, no auto-assign, last
    // zone or float default (F415).
    if (reason == PhosphorEngine::RestoreReason::DesktopReapply) {
        answerHeldZone();
        return;
    }
    // An open's continuation resolves its rules against the screen it arrived
    // on: the verdict cached at the spawn screen answered for the wrong one
    // (F332).
    if (reason == PhosphorEngine::RestoreReason::DesktopArrival) {
        m_adaptor->evictRuleVerdicts(windowId);
    }

    // Claim this instance's placement record BEFORE anything reads one. At
    // login every uuid is fresh, so every selector over this appId bucket falls
    // to the same pool of records. Claiming once pins WHICH record belongs to
    // this instance for the whole open and for the re-drives that follow it:
    // without it an already-home window could consume a sibling's record
    // outright, and a multi-window app's later re-resolve could answer from a
    // different record than the one its open used. The claim takes only a
    // record the OPENING screen's engine can restore on this output (the
    // reopen contract), so two instances of one app on two monitors each keep
    // the record of their own monitor instead of the first opener taking the
    // newest wherever it lives.
    //
    // Open AND PendingSweep. The sweep re-resolves windows that are already
    // OPEN, but for the placement store it CAN still be a first touch: a window
    // whose open resolve arrived before the daemon was ready never got past the
    // readiness gate above, so it never claimed anything. For one that did
    // already claim, the claim is idempotent and this is harmless. Without
    // this the pairing guard was absent in exactly the slow-daemon login the
    // feature exists for. The other drivers are re-entries (Unminimize,
    // DesktopArrival) or run with stable uuids (DaemonRestartSweep).
    if (isOpen || reason == PhosphorEngine::RestoreReason::PendingSweep) {
        svc->claimPlacementForOpen(windowId, screenId,
                                   m_engine->isSnapModeScreen(screenId)
                                       ? QString(PhosphorEngine::WindowPlacement::snapEngineId())
                                       : svc->owningModeEngineId(windowId, screenId));
    }

    // Engine-neutral RouteToDesktop runs first — a window can be routed to a
    // desktop whether or not it snaps (and even when it doesn't match a
    // SnapToZone rule at all), so it must not sit behind the shouldSnap
    // early-return below. First placements only, the same set that claims:
    // the sweeps and the unminimize re-drive act on a window the user may
    // have moved to another desktop since, and routing it again yanks it
    // back at every daemon restart; the desktop-arrival re-drive IS the
    // landing of a move already made. (The tiling dispatch routes once per
    // open for the same reason.)
    const bool firstPlacement = isOpen || reason == PhosphorEngine::RestoreReason::PendingSweep;
    if (firstPlacement) {
        m_adaptor->applyOpenDesktopRouting(windowId, screenId);
    }

    const PhosphorEngine::WindowKind kind = PhosphorEngine::clampWindowKindFromWire(windowKind);
    SnapResult result = m_engine->resolveWindowRestore(windowId, screenId, sticky, kind, reason);

    if (!result.shouldSnap) {
        // A window the engine already holds in a zone on the desktop it landed
        // on: the engine's "already assigned" no-op is right about the
        // assignment, and the rect is what the arrival needs (see above).
        if (reason == PhosphorEngine::RestoreReason::DesktopArrival && answerHeldZone()) {
            return;
        }
        // Nothing snapped this window. A bare RouteToScreen rule (move-to-monitor
        // with no SnapToZone) takes effect here, deliberately AFTER the snap/float
        // restore has had its chance: a SnapToZone restore or a remembered snap
        // already returned shouldSnap=true above (so the route never fights a
        // snap), and the explicit route wins over a remembered float position (it
        // applies the final geometry). A route WITH SnapToZone moved+snapped on
        // the target via the placement directive and never reaches here.
        // Gated like the desktop route above, plus the desktop-arrival
        // continuation: a re-drive of a visible window (the sweeps, an
        // unminimize) must not pull a window the user dragged to another
        // monitor back to its rule's. Nothing else moves the window to another
        // screen here: under the reopen contract no engine reclaims a window
        // whose record lives on another monitor.
        if (firstPlacement || reason == PhosphorEngine::RestoreReason::DesktopArrival) {
            m_adaptor->applyOpenScreenRouting(windowId, screenId, reason);
        }
        return;
    }

    // "Focus new windows" focuses a genuine open only: every other reason
    // re-places a window that was already on screen (or, for a desktop
    // arrival, one that opened while its desktop was away), so KWin keeps
    // its focus pick.
    applySnapResult(result, windowId, snapX, snapY, snapWidth, snapHeight, shouldSnap,
                    reason == PhosphorEngine::RestoreReason::Open ? SnapIntent::AutoRestored
                                                                  : SnapIntent::AutoReplaced);
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
}

bool SnapAdaptor::snapPermittedForContext(const QString& windowId, const QString& screenId, int virtualDesktop) const
{
    // Global snapping kill-switch — see discussion #461 item 2. Every answer
    // this facade's slots give funnels through here, so a single gate
    // suppresses all auto-snap paths when the user has turned snapping off
    // entirely. Mirrors the engine-internal gate in
    // SnapEngine::resolveWindowRestore.
    if (m_settings && !m_settings->snappingEnabled()) {
        qCInfo(lcDbusWindow) << "snapPermittedForContext: refusing auto-snap of" << windowId
                             << "— snapping is globally disabled";
        return false;
    }

    // Disabled-context gate. The interactive drag path (WindowDragAdaptor)
    // and autotile (Daemon::updateEngineScreens) already refuse to place
    // windows on a monitor / desktop / activity the user marked disabled.
    // The auto-snap-on-open restore path did not, so windows still snapped on
    // a disabled context (discussion #461). Gating here covers all restore
    // entry points in one place.
    if (!m_settings || screenId.isEmpty() || !m_contextResolver) {
        return true;
    }
    // Gate against the DESTINATION screen's actual mode. A restore result
    // can cross-screen-migrate (placement rule / session restore) onto a screen
    // whose mode differs from the caller's, so the disable list to consult
    // is the one for that screen's mode — not a hard-coded Snapping.
    //
    // The destination DESKTOP is @p virtualDesktop when the answer was routed
    // there (a RouteToDesktop placement, calculateSnapToPlacementRule),
    // otherwise the current desktop (every other calculator opens the window on
    // the current desktop, or refuses outright on a saved-desktop mismatch).
    // For a routed result handleForPersisted composes the explicit destination
    // desktop, so the disable check keys on the desktop the window will actually
    // land on rather than the live current desktop; for a non-routed one
    // (virtualDesktop == 0) handleFor is exact, pulling (currentVirtualDesktop,
    // currentActivity) from the daemon's VDM/AM — the same values the snap engine
    // sees — and routing the screen through the mode provider in one snapshot.
    const auto handle = virtualDesktop >= 1
        ? m_contextResolver->handleForPersisted(screenId, virtualDesktop, m_contextResolver->currentActivity())
        : m_contextResolver->handleFor(screenId);
    if (m_contextResolver->isDisabled(handle)) {
        qCInfo(lcDbusWindow) << "snapPermittedForContext: refusing auto-snap of" << windowId
                             << "— PlasmaZones is disabled for screen" << screenId;
        return false;
    }
    return true;
}

bool SnapAdaptor::applySnapResult(const SnapResult& result, const QString& windowId, int& snapX, int& snapY,
                                  int& snapWidth, int& snapHeight, bool& shouldSnap, SnapIntent intent)
{
    snapX = snapY = snapWidth = snapHeight = 0;
    shouldSnap = false;

    if (!m_adaptor || !m_adaptor->service() || !m_engine) {
        return false;
    }

    if (!snapPermittedForContext(windowId, result.screenId, result.virtualDesktop)) {
        return false;
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

    // commitSnap runs the full orchestration: clears any pre-existing
    // floating state (emits windowFloatingClearedForSnap which the adaptor
    // relays as windowFloatingChanged), assigns to zone(s), emits state
    // change. The auto-snapped mark comes AFTER it, into the store the commit
    // placed the window in: marked first, it parked on the global holder,
    // and the commit's first placement evicted it from there (F385).
    if (zoneIds.size() > 1) {
        m_engine->commitMultiZoneSnap(windowId, zoneIds, result.screenId, intent, result.virtualDesktop);
    } else {
        m_engine->commitSnap(windowId, zoneIds.first(), result.screenId, intent, result.virtualDesktop);
    }
    m_adaptor->service()->markAsAutoSnapped(windowId);
    // Focus-new-windows is decided inside SnapEngine::commitSnapImpl from the
    // intent: AutoRestored may focus, AutoReplaced never does.
    return true;
}

} // namespace PlasmaZones
