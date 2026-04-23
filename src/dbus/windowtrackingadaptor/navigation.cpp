// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// Snap-mode navigation forwarders (moveWindowToAdjacentZone, focusAdjacentZone,
// swapWindowWithAdjacentZone, pushToEmptyZone, snapToZoneByNumber,
// cycleWindowsInZone, restoreWindowSize, rotateWindowsInLayout) moved to
// SnapAdaptor (src/dbus/snapadaptor/navigation.cpp).
//
// resolveSnapModeScreensForResnap stays — it's a public utility used by
// SnapEngine's navigation methods. SnapAdaptor has its own copy that
// delegates here via m_adaptor.
//
// requestMoveSpecificWindowToZone and reportNavigationFeedback stay — they are
// pure signal emitters used cross-mode.

#include "../windowtrackingadaptor.h"
#include "../../core/logging.h"
#include "../../core/screenmoderouter.h"
#include "../../core/windowtrackingservice.h"
#include <PhosphorScreens/Manager.h>
#include <PhosphorScreens/VirtualScreen.h>

namespace PlasmaZones {

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
