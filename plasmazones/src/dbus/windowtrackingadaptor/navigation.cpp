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

#include "windowtrackingadaptor.h"
#include "core/platform/logging.h"
#include "core/interfaces/interfaces.h"
#include "core/resolve/screenmoderouter.h"
#include <PhosphorZones/LayoutRegistry.h>
#include <PhosphorPlacement/WindowTrackingService.h>
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

    // Every caller is a system resnap: nothing with snapping switched off,
    // nothing in a context the user disabled (F469).
    if (m_settings && !m_settings->snappingEnabled()) {
        return {};
    }
    if (m_screenModeRouter) {
        candidates = m_screenModeRouter->partitionByMode(candidates).snap;
    }
    const QString activity = m_layoutManager->currentActivity();
    candidates.removeIf([this, &activity](const QString& screenId) {
        return isPersistedContextDisabled(screenId, currentDesktopForScreen(screenId), activity);
    });
    return candidates;
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
