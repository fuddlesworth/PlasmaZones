// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "../windowtrackingadaptor.h"
#include "internal.h"
#include "../../snap/SnapEngine.h"
#include "../../core/logging.h"

namespace PlasmaZones {

void WindowTrackingAdaptor::moveWindowToAdjacentZone(const QString& direction)
{
    qCInfo(lcDbusWindow) << "moveWindowToAdjacentZone called with direction:" << direction;

    if (!validateDirection(direction, QStringLiteral("move"))) {
        return;
    }

    if (m_snapEngine) {
        m_snapEngine->moveInDirection(direction);
    }
}

void WindowTrackingAdaptor::focusAdjacentZone(const QString& direction)
{
    qCInfo(lcDbusWindow) << "focusAdjacentZone called with direction:" << direction;

    if (!validateDirection(direction, QStringLiteral("focus"))) {
        return;
    }

    if (m_snapEngine) {
        m_snapEngine->focusInDirection(direction, QStringLiteral("focus"));
    }
}

void WindowTrackingAdaptor::pushToEmptyZone(const QString& screenName)
{
    qCInfo(lcDbusWindow) << "pushToEmptyZone called, screen:" << screenName;
    if (m_snapEngine) {
        m_snapEngine->pushToEmptyZone(screenName);
    }
}

void WindowTrackingAdaptor::restoreWindowSize()
{
    qCInfo(lcDbusWindow) << "restoreWindowSize called";
    Q_EMIT restoreWindowRequested();
}

void WindowTrackingAdaptor::toggleWindowFloat()
{
    qCInfo(lcDbusWindow) << "toggleWindowFloat called";
    Q_EMIT toggleWindowFloatRequested(true);
}

void WindowTrackingAdaptor::swapWindowWithAdjacentZone(const QString& direction)
{
    qCInfo(lcDbusWindow) << "swapWindowWithAdjacentZone called with direction:" << direction;

    if (!validateDirection(direction, QStringLiteral("swap"))) {
        return;
    }

    if (m_snapEngine) {
        m_snapEngine->swapInDirection(direction, QStringLiteral("swap"));
    }
}

void WindowTrackingAdaptor::snapToZoneByNumber(int zoneNumber, const QString& screenName)
{
    qCInfo(lcDbusWindow) << "snapToZoneByNumber called with zone number:" << zoneNumber << "screen:" << screenName;

    if (zoneNumber < 1 || zoneNumber > 9) {
        qCWarning(lcDbusWindow) << "Invalid zone number:" << zoneNumber << "(must be 1-9)";
        Q_EMIT navigationFeedback(false, QStringLiteral("snap"), QStringLiteral("invalid_zone_number"), QString(),
                                  QString(), QString());
        return;
    }

    if (m_snapEngine) {
        m_snapEngine->moveToPosition(QString(), zoneNumber, screenName);
    }
}

void WindowTrackingAdaptor::rotateWindowsInLayout(bool clockwise, const QString& screenName)
{
    qCDebug(lcDbusWindow) << "rotateWindowsInLayout called, clockwise:" << clockwise << "screen:" << screenName;
    if (m_snapEngine) {
        m_snapEngine->rotateWindows(clockwise, screenName);
    }
}

void WindowTrackingAdaptor::cycleWindowsInZone(bool forward)
{
    qCDebug(lcDbusWindow) << "cycleWindowsInZone called, forward:" << forward;
    if (m_snapEngine) {
        m_snapEngine->cycleWindowsInZone(forward);
    }
}

void WindowTrackingAdaptor::resnapToNewLayout()
{
    qCDebug(lcDbusWindow) << "resnapToNewLayout called";
    if (m_snapEngine) {
        m_snapEngine->resnapToNewLayout();
    }
}

void WindowTrackingAdaptor::resnapCurrentAssignments(const QString& screenFilter)
{
    qCDebug(lcDbusWindow) << "resnapCurrentAssignments called, screen:"
                          << (screenFilter.isEmpty() ? QStringLiteral("all") : screenFilter);
    if (m_snapEngine) {
        m_snapEngine->resnapCurrentAssignments(screenFilter);
    }
}

void WindowTrackingAdaptor::resnapFromAutotileOrder(const QStringList& autotileWindowOrder, const QString& screenName)
{
    qCDebug(lcDbusWindow) << "resnapFromAutotileOrder called with" << autotileWindowOrder.size()
                          << "windows for screen:" << screenName;
    if (m_snapEngine) {
        m_snapEngine->resnapFromAutotileOrder(autotileWindowOrder, screenName);
    }
}

void WindowTrackingAdaptor::snapAllWindows(const QString& screenName)
{
    qCDebug(lcDbusWindow) << "snapAllWindows called for screen:" << screenName;
    if (m_snapEngine) {
        m_snapEngine->snapAllWindows(screenName);
    }
}

void WindowTrackingAdaptor::requestMoveSpecificWindowToZone(const QString& windowId, const QString& zoneId,
                                                            const QString& geometryJson)
{
    qCDebug(lcDbusWindow) << "requestMoveSpecificWindowToZone: window=" << windowId << "zone=" << zoneId;
    Q_EMIT moveSpecificWindowToZoneRequested(windowId, zoneId, geometryJson);
}

QString WindowTrackingAdaptor::calculateSnapAllWindows(const QStringList& windowIds, const QString& screenName)
{
    qCDebug(lcDbusWindow) << "calculateSnapAllWindows called with" << windowIds.size()
                          << "windows on screen:" << screenName;
    if (m_snapEngine) {
        return m_snapEngine->calculateSnapAllWindows(windowIds, screenName);
    }
    return QStringLiteral("[]");
}

void WindowTrackingAdaptor::reportNavigationFeedback(bool success, const QString& action, const QString& reason,
                                                     const QString& sourceZoneId, const QString& targetZoneId,
                                                     const QString& screenName)
{
    qCDebug(lcDbusWindow) << "Navigation feedback: success=" << success << "action=" << action << "reason=" << reason
                          << "sourceZone=" << sourceZoneId << "targetZone=" << targetZoneId << "screen=" << screenName;
    Q_EMIT navigationFeedback(success, action, reason, sourceZoneId, targetZoneId, screenName);
}

bool WindowTrackingAdaptor::validateDirection(const QString& direction, const QString& action)
{
    if (direction.isEmpty()) {
        qCWarning(lcDbusWindow) << "Cannot" << action << "- empty direction";
        Q_EMIT navigationFeedback(false, action, QStringLiteral("invalid_direction"), QString(), QString(), QString());
        return false;
    }
    return true;
}

} // namespace PlasmaZones
