// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "../snapadaptor.h"
#include "../windowtrackingadaptor.h"
#include "core/interfaces/interfaces.h"
#include "core/platform/logging.h"
#include <PhosphorPlacement/WindowTrackingService.h>
#include <PhosphorScreens/Manager.h>
#include "core/interfaces/isettings.h"
#include <PhosphorContext/ContextResolver.h>
#include <PhosphorSnapEngine/SnapEngine.h>

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
bool isSnapReadyOrWarn(PhosphorPlacement::WindowTrackingService* service, const char* method)
{
    auto* mgr = service ? service->screenManager() : nullptr;
    if (!mgr || mgr->isPanelGeometryReady()) {
        return true;
    }
    static bool warned = false;
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

    // Empty windowId is a precondition violation that the sibling slots
    // (snapToAppRule, snapToEmptyZone, resolveWindowRestore) all guard;
    // mirror their early-return so the input contract is symmetric across
    // the snap-restore family.
    if (windowId.isEmpty()) {
        return;
    }

    if (!m_adaptor || !m_adaptor->service()) {
        return;
    }

    if (!isSnapReadyOrWarn(m_adaptor->service(), "snapToLastZone")) {
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

    if (windowId.isEmpty()) {
        return;
    }

    if (!m_adaptor || !m_adaptor->service()) {
        return;
    }

    if (!isSnapReadyOrWarn(m_adaptor->service(), "snapToAppRule")) {
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

    if (windowId.isEmpty()) {
        return;
    }

    if (!m_adaptor || !m_adaptor->service()) {
        return;
    }

    if (!isSnapReadyOrWarn(m_adaptor->service(), "snapToEmptyZone")) {
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
                                       int& snapX, int& snapY, int& snapWidth, int& snapHeight, bool& shouldSnap)
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

    if (!isSnapReadyOrWarn(m_adaptor->service(), "resolveWindowRestore")) {
        return;
    }

    // Engine-neutral RouteToDesktop runs first and unconditionally — a window can
    // be routed to a desktop whether or not it snaps (and even when it doesn't
    // match a SnapToZone rule at all), so it must not sit behind the shouldSnap
    // early-return below.
    m_adaptor->applyOpenDesktopRouting(windowId, screenId);

    const PhosphorEngine::WindowKind kind = PhosphorEngine::clampWindowKindFromWire(windowKind);
    SnapResult result = m_engine->resolveWindowRestore(windowId, screenId, sticky, kind);
    if (!result.shouldSnap) {
        // Nothing snapped this window. A bare RouteToScreen rule (move-to-monitor
        // with no SnapToZone) takes effect here, deliberately AFTER the snap/float
        // restore has had its chance: a SnapToZone restore or a remembered snap
        // already returned shouldSnap=true above (so the route never fights a snap),
        // and the explicit route wins over a remembered float position (it applies
        // the final geometry). A route WITH SnapToZone moved+snapped on the target
        // via the placement directive and never reaches here.
        m_adaptor->applyOpenScreenRouting(windowId, screenId);
        return;
    }

    applySnapResult(result, windowId, snapX, snapY, snapWidth, snapHeight, shouldSnap);
    // Return value intentionally ignored: applySnapResult has already set
    // shouldSnap (false on a disabled-context refusal) and there is no
    // post-snap work in this slot to skip.
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
    // and autotile (Daemon::updateAutotileScreens) already refuse to place
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
    const QStringList zoneIds = result.zoneIds.isEmpty() ? QStringList{result.zoneId} : result.zoneIds;
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
