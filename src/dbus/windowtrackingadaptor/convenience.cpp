// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "../windowtrackingadaptor.h"
#include "../../core/logging.h"
#include "../../core/utils.h"

namespace PlasmaZones {

void WindowTrackingAdaptor::moveWindowToZone(const QString& windowId, const QString& zoneId)
{
    if (!validateWindowId(windowId, QStringLiteral("moveWindowToZone"))) {
        return;
    }

    if (zoneId.isEmpty()) {
        qCWarning(lcDbusWindow) << "moveWindowToZone: empty zone ID";
        return;
    }

    // Resolve screen for the target zone
    QString screenId = resolveScreenForSnap(QString(), zoneId);

    // Get zone geometry
    QRect geo = m_service->zoneGeometry(zoneId, screenId);
    if (!geo.isValid()) {
        qCWarning(lcDbusWindow) << "moveWindowToZone: invalid geometry for zone:" << zoneId;
        return;
    }

    // Perform snap bookkeeping
    windowSnapped(windowId, zoneId, screenId);
    m_service->recordSnapIntent(windowId, true);

    // Request compositor to apply geometry
    Q_EMIT applyGeometryRequested(windowId, geo.x(), geo.y(), geo.width(), geo.height(), zoneId, screenId, false);

    qCInfo(lcDbusWindow) << "moveWindowToZone:" << windowId << "-> zone" << zoneId << "on screen" << screenId;
}

void WindowTrackingAdaptor::swapWindowsById(const QString& windowId1, const QString& windowId2)
{
    if (!validateWindowId(windowId1, QStringLiteral("swapWindowsById (window1)"))) {
        return;
    }
    if (!validateWindowId(windowId2, QStringLiteral("swapWindowsById (window2)"))) {
        return;
    }
    if (windowId1 == windowId2) {
        qCWarning(lcDbusWindow) << "swapWindowsById: cannot swap window with itself:" << windowId1;
        return;
    }

    // Get each window's current zone
    QString zoneId1 = m_service->zoneForWindow(windowId1);
    QString zoneId2 = m_service->zoneForWindow(windowId2);

    if (zoneId1.isEmpty() || zoneId2.isEmpty()) {
        qCWarning(lcDbusWindow) << "swapWindowsById: one or both windows not snapped"
                                << "w1:" << windowId1 << "zone:" << zoneId1 << "w2:" << windowId2 << "zone:" << zoneId2;
        return;
    }

    // Get screens for each window
    QString screen1 = m_service->screenAssignments().value(windowId1);
    QString screen2 = m_service->screenAssignments().value(windowId2);

    // Get the OTHER window's zone geometry (for the swap)
    QRect geo1 = m_service->zoneGeometry(zoneId2, screen2); // window1 moves to zone2
    QRect geo2 = m_service->zoneGeometry(zoneId1, screen1); // window2 moves to zone1

    if (!geo1.isValid() || !geo2.isValid()) {
        qCWarning(lcDbusWindow) << "swapWindowsById: invalid geometry for swap";
        return;
    }

    // Update bookkeeping: window1 goes to zone2, window2 goes to zone1
    windowSnapped(windowId1, zoneId2, screen2);
    windowSnapped(windowId2, zoneId1, screen1);

    // Emit geometry requests for both
    Q_EMIT applyGeometryRequested(windowId1, geo1.x(), geo1.y(), geo1.width(), geo1.height(), zoneId2, screen2, false);
    Q_EMIT applyGeometryRequested(windowId2, geo2.x(), geo2.y(), geo2.width(), geo2.height(), zoneId1, screen1, false);

    qCInfo(lcDbusWindow) << "swapWindowsById:" << windowId1 << "<->" << windowId2 << "zones:" << zoneId1 << "<->"
                         << zoneId2;
}

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
