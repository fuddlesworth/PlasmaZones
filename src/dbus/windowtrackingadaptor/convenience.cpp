// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// moveWindowToZone, swapWindowsById moved to SnapAdaptor
// (src/dbus/snapadaptor/commit.cpp).
//
// getWindowState, getAllWindowStates stay — they are cross-mode queries
// on WTS state.

#include "../windowtrackingadaptor.h"
#include "../../core/logging.h"
#include "../../core/windowtrackingservice.h"

namespace PlasmaZones {

WindowStateEntry WindowTrackingAdaptor::getWindowState(const QString& windowId)
{
    if (windowId.isEmpty()) {
        qCWarning(lcDbusWindow) << "getWindowState: empty window ID";
        return WindowStateEntry{};
    }

    return WindowStateEntry{
        windowId,
        m_service->zoneForWindow(windowId),
        m_service->screenAssignments().value(windowId),
        m_service->isWindowFloating(windowId),
        QString(), // changeType: empty for query (not a state change event)
        m_service->zonesForWindow(windowId),
        m_service->isWindowSticky(windowId),
    };
}

WindowStateList WindowTrackingAdaptor::getAllWindowStates()
{
    WindowStateList result;

    // Collect all tracked windows: snapped + floating
    QSet<QString> allWindowIds;

    // Add snapped windows
    const auto& zoneAssignments = m_service->zoneAssignments();
    for (auto it = zoneAssignments.constBegin(); it != zoneAssignments.constEnd(); ++it) {
        allWindowIds.insert(it.key());
    }

    // Add floating windows
    const QStringList floatingWindows = m_service->floatingWindows();
    for (const QString& windowId : floatingWindows) {
        allWindowIds.insert(windowId);
    }

    // Build state for each window
    for (const QString& windowId : std::as_const(allWindowIds)) {
        result.append(WindowStateEntry{
            windowId,
            m_service->zoneForWindow(windowId),
            m_service->screenAssignments().value(windowId),
            m_service->isWindowFloating(windowId),
            QString(), // changeType: empty for query
            m_service->zonesForWindow(windowId),
            m_service->isWindowSticky(windowId),
        });
    }

    return result;
}

} // namespace PlasmaZones
