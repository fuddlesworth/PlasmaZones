// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "../windowtrackingadaptor.h"
#include "../../core/interfaces.h"
#include "../../core/logging.h"
#include "../../core/utils.h"
#include "../../core/virtualdesktopmanager.h"
#include "../../snap/SnapEngine.h"

namespace PlasmaZones {

void WindowTrackingAdaptor::snapToLastZone(const QString& windowId, const QString& windowScreenName, bool sticky,
                                           int& snapX, int& snapY, int& snapWidth, int& snapHeight, bool& shouldSnap)
{
    snapX = snapY = snapWidth = snapHeight = 0;
    shouldSnap = false;

    SnapResult result = m_service->calculateSnapToLastZone(windowId, windowScreenName, sticky);
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

    SnapResult result = m_service->calculateSnapToAppRule(windowId, windowScreenName, sticky);
    if (!result.shouldSnap) {
        return;
    }

    applySnapResult(result, windowId, snapX, snapY, snapWidth, snapHeight, shouldSnap);
    qCInfo(lcDbusWindow) << "App rule snapping window" << windowId << "to zone" << result.zoneId;
}

void WindowTrackingAdaptor::snapToEmptyZone(const QString& windowId, const QString& windowScreenName, bool sticky,
                                            int& snapX, int& snapY, int& snapWidth, int& snapHeight, bool& shouldSnap)
{
    snapX = snapY = snapWidth = snapHeight = 0;
    shouldSnap = false;

    if (windowId.isEmpty()) {
        return;
    }

    qCDebug(lcDbusWindow) << "snapToEmptyZone: windowId=" << windowId << "screen=" << windowScreenName;
    SnapResult result = m_service->calculateSnapToEmptyZone(windowId, windowScreenName, sticky);
    if (!result.shouldSnap) {
        qCDebug(lcDbusWindow) << "snapToEmptyZone: no snap";
        return;
    }

    applySnapResult(result, windowId, snapX, snapY, snapWidth, snapHeight, shouldSnap);
    qCInfo(lcDbusWindow) << "Auto-assign snapping window" << windowId << "to empty zone" << result.zoneId;
}

void WindowTrackingAdaptor::restoreToPersistedZone(const QString& windowId, const QString& screenName, bool sticky,
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

    SnapResult result = m_service->calculateRestoreFromSession(windowId, screenName, sticky);
    if (!result.shouldSnap) {
        return;
    }

    applySnapResult(result, windowId, snapX, snapY, snapWidth, snapHeight, shouldRestore);
    // Consume the pending assignment so other windows of the same class won't restore to this zone
    m_service->consumePendingAssignment(windowId);
    qCInfo(lcDbusWindow) << "Restoring window" << windowId << "to zone(s)" << result.zoneIds;
}

void WindowTrackingAdaptor::resolveWindowRestore(const QString& windowId, const QString& screenName, bool sticky,
                                                 int& snapX, int& snapY, int& snapWidth, int& snapHeight,
                                                 bool& shouldSnap)
{
    snapX = snapY = snapWidth = snapHeight = 0;
    shouldSnap = false;

    if (windowId.isEmpty() || screenName.isEmpty()) {
        return;
    }

    // Delegate to SnapEngine which owns the 4-level fallback chain.
    // SnapEngine::resolveWindowRestore() is pure decision logic — it returns
    // a SnapResult without side effects, so we apply it here for D-Bus compat.
    if (!m_snapEngine) {
        qCWarning(lcDbusWindow) << "resolveWindowRestore: no SnapEngine available";
        return;
    }

    SnapResult result = m_snapEngine->resolveWindowRestore(windowId, screenName, sticky);
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
    if (!m_lastCursorScreenName.isEmpty()) {
        return m_lastCursorScreenName;
    }
    return m_lastActiveScreenName;
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
    clearFloatingStateForSnap(windowId, result.screenName);

    int currentDesktop = m_virtualDesktopManager ? m_virtualDesktopManager->currentDesktop() : 0;
    if (result.zoneIds.size() > 1) {
        m_service->assignWindowToZones(windowId, result.zoneIds, result.screenName, currentDesktop);
    } else {
        m_service->assignWindowToZone(windowId, result.zoneId, result.screenName, currentDesktop);
    }
}

} // namespace PlasmaZones
