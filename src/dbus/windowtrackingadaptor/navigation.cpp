// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "../windowtrackingadaptor.h"
#include "../../config/settings.h"
#include <PhosphorEngineApi/IPlacementEngine.h>
#include "../../core/logging.h"
#include <PhosphorZones/LayoutRegistry.h>
#include <PhosphorScreens/Manager.h>
#include "../../core/screenmoderouter.h"
#include "../../core/utils.h"
#include <PhosphorScreens/VirtualScreen.h>
#include "../../core/windowtrackingservice.h"
#include "../../snap/SnapEngine.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

namespace PlasmaZones {

// ═══════════════════════════════════════════════════════════════════════════════
// Navigation forwarders.
//
// These methods forward shortcut-driven navigation requests to SnapEngine.
// The resnap/snap-all/batch methods moved to SnapAdaptor
// (src/dbus/snapadaptor/navigation.cpp).
// ═══════════════════════════════════════════════════════════════════════════════

void WindowTrackingAdaptor::moveWindowToAdjacentZone(const QString& direction)
{
    if (m_snapEngine) {
        m_snapEngine->moveFocusedInDirection(direction, NavigationContext{});
    }
}

void WindowTrackingAdaptor::focusAdjacentZone(const QString& direction)
{
    if (m_snapEngine) {
        m_snapEngine->focusInDirection(direction, NavigationContext{});
    }
}

void WindowTrackingAdaptor::pushToEmptyZone(const QString& screenId)
{
    if (m_snapEngine) {
        m_snapEngine->pushFocusedToEmptyZone(NavigationContext{QString(), screenId});
    }
}

void WindowTrackingAdaptor::restoreWindowSize()
{
    if (m_snapEngine) {
        m_snapEngine->restoreFocusedWindow(NavigationContext{});
    }
}

void WindowTrackingAdaptor::swapWindowWithAdjacentZone(const QString& direction)
{
    if (m_snapEngine) {
        m_snapEngine->swapFocusedInDirection(direction, NavigationContext{});
    }
}

void WindowTrackingAdaptor::snapToZoneByNumber(int zoneNumber, const QString& screenId)
{
    if (m_snapEngine) {
        m_snapEngine->moveFocusedToPosition(zoneNumber, NavigationContext{QString(), screenId});
    }
}

void WindowTrackingAdaptor::rotateWindowsInLayout(bool clockwise, const QString& screenId)
{
    if (m_snapEngine) {
        m_snapEngine->rotateWindowsInLayout(clockwise, screenId);
    }
}

void WindowTrackingAdaptor::cycleWindowsInZone(bool forward)
{
    if (m_snapEngine) {
        m_snapEngine->cycleFocus(forward, NavigationContext{});
    }
}

// resolveSnapModeScreensForResnap stays on WTA — it's a public utility used
// by SnapEngine's navigation methods. SnapAdaptor has its own copy that
// delegates to this one via the m_adaptor pointer.
QStringList WindowTrackingAdaptor::resolveSnapModeScreensForResnap(const QString& screenFilter) const
{
    QStringList candidates;
    if (!screenFilter.isEmpty()) {
        if (PhosphorIdentity::VirtualScreenId::isVirtual(screenFilter)) {
            candidates.append(screenFilter);
        } else if (auto* mgr = m_service->screenManager()) {
            candidates = mgr->virtualScreenIdsFor(screenFilter);
        } else {
            candidates.append(screenFilter);
        }
    } else if (auto* mgr = m_service->screenManager()) {
        candidates = mgr->effectiveScreenIds();
    }

    if (!m_screenModeRouter) {
        return candidates;
    }
    return m_screenModeRouter->partitionByMode(candidates).snap;
}

void WindowTrackingAdaptor::requestMoveSpecificWindowToZone(const QString& windowId, const QString& zoneId,
                                                            const QRect& geometry)
{
    qCDebug(lcDbusWindow) << "requestMoveSpecificWindowToZone: window=" << windowId << "zone=" << zoneId;
    Q_EMIT moveSpecificWindowToZoneRequested(windowId, zoneId, geometry.x(), geometry.y(), geometry.width(),
                                             geometry.height());
}

void WindowTrackingAdaptor::reportNavigationFeedback(bool success, const QString& action, const QString& reason,
                                                     const QString& sourceZoneId, const QString& targetZoneId,
                                                     const QString& screenId)
{
    qCDebug(lcDbusWindow) << "Navigation feedback: success=" << success << "action=" << action << "reason=" << reason
                          << "sourceZone=" << sourceZoneId << "targetZone=" << targetZoneId << "screen=" << screenId;
    Q_EMIT navigationFeedback(success, action, reason, sourceZoneId, targetZoneId, screenId);
}

} // namespace PlasmaZones
