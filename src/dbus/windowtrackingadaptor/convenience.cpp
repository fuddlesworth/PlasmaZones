// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// moveWindowToZone, swapWindowsById moved to SnapAdaptor
// (src/dbus/snapadaptor/commit.cpp).
//
// getWindowState, getAllWindowStates stay — they are cross-mode queries
// on WTS state.

#include "../windowtrackingadaptor.h"
#include "../../core/logging.h"
#include <PhosphorPlacement/WindowTrackingService.h>

namespace PlasmaZones {

PhosphorProtocol::WindowStateEntry WindowTrackingAdaptor::getWindowState(const QString& windowId)
{
    if (windowId.isEmpty()) {
        qCWarning(lcDbusWindow) << "getWindowState: empty window ID";
        return PhosphorProtocol::WindowStateEntry{};
    }

    return PhosphorProtocol::WindowStateEntry{
        windowId,
        m_service->zoneForWindow(windowId),
        m_service->screenForWindow(windowId),
        m_service->isWindowFloating(windowId),
        QString(), // changeType: empty for query (not a state change event)
        m_service->zonesForWindow(windowId),
        m_service->isWindowSticky(windowId),
    };
}

PhosphorProtocol::WindowStateList WindowTrackingAdaptor::getAllWindowStates()
{
    PhosphorProtocol::WindowStateList result;

    // Collect all tracked windows: snapped + floating
    QSet<QString> allWindowIds;

    // Add snapped windows
    const QStringList snappedWindows = m_service->snappedWindows();
    for (const QString& windowId : snappedWindows) {
        allWindowIds.insert(windowId);
    }

    // Add floating windows
    const QStringList floatingWindows = m_service->floatingWindows();
    for (const QString& windowId : floatingWindows) {
        allWindowIds.insert(windowId);
    }

    // Build state for each window
    for (const QString& windowId : std::as_const(allWindowIds)) {
        result.append(PhosphorProtocol::WindowStateEntry{
            windowId,
            m_service->zoneForWindow(windowId),
            m_service->screenForWindow(windowId),
            m_service->isWindowFloating(windowId),
            QString(), // changeType: empty for query
            m_service->zonesForWindow(windowId),
            m_service->isWindowSticky(windowId),
        });
    }

    return result;
}

} // namespace PlasmaZones
