// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Auto-snap logic and snap tracking helpers.
// Part of WindowTrackingService — split from windowtrackingservice.cpp for SRP.

#include "../windowtrackingservice.h"
#include "../interfaces.h"
#include <PhosphorZones/Layout.h>
#include <PhosphorZones/SnapState.h>
#include <PhosphorZones/Zone.h>
#include <PhosphorZones/LayoutRegistry.h>
#include "../virtualdesktopmanager.h"
#include <PhosphorScreens/VirtualScreen.h>
#include "../utils.h"
#include <PhosphorScreens/Manager.h>
#include "../logging.h"
#include <QGuiApplication>
#include <QScreen>
#include <QSet>
#include <QUuid>
#include <PhosphorScreens/ScreenIdentity.h>

namespace PlasmaZones {

// ═══════════════════════════════════════════════════════════════════════════════
// Auto-Snap Logic
//
// calculateSnapToAppRule, calculateSnapToLastZone, calculateSnapToEmptyZone,
// calculateRestoreFromSession moved to SnapEngine (src/snap/snapengine/calculate.cpp).
// ═══════════════════════════════════════════════════════════════════════════════

void WindowTrackingService::recordSnapIntent(const QString& windowId, bool wasUserInitiated)
{
    if (wasUserInitiated) {
        QString windowClass = currentAppIdFor(windowId);
        if (!windowClass.isEmpty()) {
            m_snapState->recordSnapIntent(windowClass, true);
            markDirty(DirtyUserSnapped);
        }
    }
}

void WindowTrackingService::updateLastUsedZone(const QString& zoneId, const QString& screenId,
                                               const QString& windowClass, int virtualDesktop)
{
    m_snapState->updateLastUsedZone(zoneId, screenId, windowClass, virtualDesktop);
    markDirty(DirtyLastUsedZone);
}

// ═══════════════════════════════════════════════════════════════════════════════
// Snap commit orchestration — moved out of WindowTrackingAdaptor.
//
// These methods used to live in WindowTrackingAdaptor::windowSnapped /
// windowSnappedMultiZone / windowUnsnapped, where they orchestrated a
// sequence of WTS primitive calls plus a D-Bus signal emit. That made
// WTA a partial snap engine rather than a thin facade. The orchestration
// is pure state-management work that belongs on WTS; WTA retains its
// D-Bus slot entry points but forwards to these methods and relays the
// WTS signals to its own D-Bus signals at connection wiring time.
// ═══════════════════════════════════════════════════════════════════════════════

// commitSnap, commitMultiZoneSnap, uncommitSnap, applyBatchAssignments
// moved to SnapEngine (src/snap/snapengine/commit.cpp).

void WindowTrackingService::markWindowReported(const QString& windowId)
{
    if (!windowId.isEmpty()) {
        m_effectReportedWindows.insert(windowId);
    }
}

void WindowTrackingService::markAsAutoSnapped(const QString& windowId)
{
    if (!windowId.isEmpty()) {
        m_snapState->markAsAutoSnapped(windowId);
    }
}

bool WindowTrackingService::isAutoSnapped(const QString& windowId) const
{
    return m_snapState->isAutoSnapped(windowId);
}

bool WindowTrackingService::clearAutoSnapped(const QString& windowId)
{
    return m_snapState->clearAutoSnapped(windowId);
}

bool WindowTrackingService::consumePendingAssignment(const QString& windowId)
{
    // Pop the oldest pending-restore entry for this window's live appId.
    // Single authoritative implementation — see header for why earlier
    // consumePendingAssignment / clearStalePendingAssignment twins were
    // merged. Callers that don't care about the result ignore the bool.
    const QString appId = currentAppIdFor(windowId);
    auto it = m_pendingRestoreQueues.find(appId);
    if (it == m_pendingRestoreQueues.end() || it->isEmpty()) {
        return false;
    }
    it->removeFirst();
    if (it->isEmpty()) {
        m_pendingRestoreQueues.erase(it);
    }
    qCDebug(lcCore) << "Consumed pending assignment for" << appId
                    << "remaining:" << m_pendingRestoreQueues.value(appId).size();
    markDirty(DirtyPendingRestores);
    return true;
}

} // namespace PlasmaZones
