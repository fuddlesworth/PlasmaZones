// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "../windowtrackingadaptor.h"
#include "../../core/interfaces.h"
#include "../../core/logging.h"
#include "../../core/screenmanager.h"
#include "../../core/utils.h"
#include "../../core/virtualdesktopmanager.h"
#include "../../snap/SnapEngine.h"

namespace PlasmaZones {

namespace {
// Non-blocking startup gate shared by all synchronous snap D-Bus methods.
//
// Rationale: these slots return zone geometry synchronously to the KWin effect.
// Before the first panel D-Bus query completes, ScreenManager's availability cache
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
//
// Unlike a blocking wait, this short-circuit introduces no nested event loop, no
// reentrancy hazard, and no bounded-timeout guesswork. The tradeoff is that snap
// restore becomes best-effort if the effect-side gate is broken; we prefer a visible
// warning over a silent jump to wrong coordinates.
bool isSnapReadyOrWarn(const char* method)
{
    if (!ScreenManager::instance() || ScreenManager::isPanelGeometryReady()) {
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

void WindowTrackingAdaptor::snapToLastZone(const QString& windowId, const QString& windowScreenId, bool sticky,
                                           int& snapX, int& snapY, int& snapWidth, int& snapHeight, bool& shouldSnap)
{
    snapX = snapY = snapWidth = snapHeight = 0;
    shouldSnap = false;

    if (!isSnapReadyOrWarn("snapToLastZone")) {
        return;
    }

    SnapResult result = m_service->calculateSnapToLastZone(windowId, windowScreenId, sticky);
    if (!result.shouldSnap) {
        return;
    }

    applySnapResult(result, windowId, snapX, snapY, snapWidth, snapHeight, shouldSnap);
    qCInfo(lcDbusWindow) << "Snapping new window" << windowId << "to last used zone" << result.zoneId;
}

void WindowTrackingAdaptor::snapToAppRule(const QString& windowId, const QString& windowScreenName, bool sticky,
                                          int& snapX, int& snapY, int& snapWidth, int& snapHeight, bool& shouldSnap)
{
    snapX = snapY = snapWidth = snapHeight = 0;
    shouldSnap = false;

    if (windowId.isEmpty()) {
        return;
    }

    if (!isSnapReadyOrWarn("snapToAppRule")) {
        return;
    }

    SnapResult result = m_service->calculateSnapToAppRule(windowId, windowScreenName, sticky);
    if (!result.shouldSnap) {
        return;
    }

    applySnapResult(result, windowId, snapX, snapY, snapWidth, snapHeight, shouldSnap);
    qCInfo(lcDbusWindow) << "App rule snapping window" << windowId << "to zone" << result.zoneId;
}

void WindowTrackingAdaptor::snapToEmptyZone(const QString& windowId, const QString& windowScreenId, bool sticky,
                                            int& snapX, int& snapY, int& snapWidth, int& snapHeight, bool& shouldSnap)
{
    snapX = snapY = snapWidth = snapHeight = 0;
    shouldSnap = false;

    if (windowId.isEmpty()) {
        return;
    }

    if (!isSnapReadyOrWarn("snapToEmptyZone")) {
        return;
    }

    qCDebug(lcDbusWindow) << "snapToEmptyZone: windowId=" << windowId << "screen=" << windowScreenId;
    SnapResult result = m_service->calculateSnapToEmptyZone(windowId, windowScreenId, sticky);
    if (!result.shouldSnap) {
        qCDebug(lcDbusWindow) << "snapToEmptyZone: no snap";
        return;
    }

    applySnapResult(result, windowId, snapX, snapY, snapWidth, snapHeight, shouldSnap);
    qCInfo(lcDbusWindow) << "Auto-assign snapping window" << windowId << "to empty zone" << result.zoneId;
}

void WindowTrackingAdaptor::restoreToPersistedZone(const QString& windowId, const QString& screenId, bool sticky,
                                                   int& snapX, int& snapY, int& snapWidth, int& snapHeight,
                                                   bool& shouldRestore)
{
    snapX = snapY = snapWidth = snapHeight = 0;
    shouldRestore = false;

    if (m_settings && !m_settings->restoreWindowsToZonesOnLogin()) {
        qCDebug(lcDbusWindow) << "Session zone restoration disabled by setting";
        return;
    }

    if (windowId.isEmpty()) {
        return;
    }

    if (!isSnapReadyOrWarn("restoreToPersistedZone")) {
        return;
    }

    SnapResult result = m_service->calculateRestoreFromSession(windowId, screenId, sticky);
    if (!result.shouldSnap) {
        return;
    }

    applySnapResult(result, windowId, snapX, snapY, snapWidth, snapHeight, shouldRestore);
    // Consume the pending assignment so other windows of the same class won't restore to this zone
    m_service->consumePendingAssignment(windowId);
    qCInfo(lcDbusWindow) << "Restoring window" << windowId << "to zone(s)" << result.zoneIds;
}

void WindowTrackingAdaptor::resolveWindowRestore(const QString& windowId, const QString& screenId, bool sticky,
                                                 int& snapX, int& snapY, int& snapWidth, int& snapHeight,
                                                 bool& shouldSnap)
{
    snapX = snapY = snapWidth = snapHeight = 0;
    shouldSnap = false;

    if (windowId.isEmpty() || screenId.isEmpty()) {
        return;
    }

    // No caller-screen mode gate here. The window may land on an autotile
    // screen but still need to be restored to a saved zone on a *different*
    // (snap-mode) screen — e.g. firefox was closed on VS0 (snap) and KWin
    // session-restore placed it on VS1 (autotile) on the same monitor.
    // SnapEngine::resolveWindowRestore consults the PendingRestore queue,
    // which records the saved screen, and returns a SnapResult whose
    // screenId points at the saved zone's screen (possibly != screenId).
    // If the saved screen is also autotile (or no saved zone exists), the
    // engine correctly returns noSnap and autotile on the caller's screen
    // claims the window via its own windowOpened path.
    if (!m_snapEngine) {
        qCWarning(lcDbusWindow) << "resolveWindowRestore: no SnapEngine available";
        return;
    }

    if (!isSnapReadyOrWarn("resolveWindowRestore")) {
        return;
    }

    SnapResult result = m_snapEngine->resolveWindowRestore(windowId, screenId, sticky);
    if (!result.shouldSnap) {
        return;
    }

    applySnapResult(result, windowId, snapX, snapY, snapWidth, snapHeight, shouldSnap);
}

void WindowTrackingAdaptor::recordSnapIntent(const QString& windowId, bool wasUserInitiated)
{
    if (windowId.isEmpty()) {
        return;
    }
    // Delegate to service
    m_service->recordSnapIntent(windowId, wasUserInitiated);
}

bool WindowTrackingAdaptor::validateWindowId(const QString& windowId, const QString& operation) const
{
    if (windowId.isEmpty()) {
        qCWarning(lcDbusWindow) << "Cannot" << operation << "- empty window ID";
        return false;
    }
    return true;
}

QString WindowTrackingAdaptor::resolveScreenForSnap(const QString& callerScreen, const QString& zoneId) const
{
    if (!callerScreen.isEmpty()) {
        return callerScreen;
    }
    QString detected = detectScreenForZone(zoneId);
    if (!detected.isEmpty()) {
        return detected;
    }
    // Tertiary: use cursor or active window screen
    if (!m_lastCursorScreenId.isEmpty()) {
        return m_lastCursorScreenId;
    }
    return m_lastActiveScreenId;
}

void WindowTrackingAdaptor::applySnapResult(const SnapResult& result, const QString& windowId, int& snapX, int& snapY,
                                            int& snapWidth, int& snapHeight, bool& shouldSnap)
{
    snapX = result.geometry.x();
    snapY = result.geometry.y();
    snapWidth = result.geometry.width();
    snapHeight = result.geometry.height();
    shouldSnap = true;

    m_service->markAsAutoSnapped(windowId);
    clearFloatingStateForSnap(windowId, result.screenId);

    int currentDesktop = m_virtualDesktopManager ? m_virtualDesktopManager->currentDesktop() : 0;
    if (result.zoneIds.size() > 1) {
        m_service->assignWindowToZones(windowId, result.zoneIds, result.screenId, currentDesktop);
    } else {
        m_service->assignWindowToZone(windowId, result.zoneId, result.screenId, currentDesktop);
    }
}

} // namespace PlasmaZones
