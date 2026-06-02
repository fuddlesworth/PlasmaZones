// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Auto-snap logic and snap tracking helpers.
// Part of WindowTrackingService — split from windowtrackingservice.cpp for SRP.

#include <PhosphorPlacement/WindowTrackingService.h>
#include "placementutils.h"

#include <PhosphorZones/Layout.h>
#include <PhosphorSnapEngine/SnapState.h>
#include <PhosphorZones/Zone.h>
#include <PhosphorZones/LayoutRegistry.h>
#include <PhosphorWorkspaces/VirtualDesktopManager.h>
#include <PhosphorScreens/VirtualScreen.h>
#include <PhosphorIdentity/WindowId.h>
#include <PhosphorScreens/Manager.h>
#include "placementlogging.h"
#include <QGuiApplication>
#include <QScreen>
#include <QSet>
#include <QUuid>
#include <PhosphorScreens/ScreenIdentity.h>

namespace PhosphorPlacement {

// ═══════════════════════════════════════════════════════════════════════════════
// Auto-Snap Logic
// ═══════════════════════════════════════════════════════════════════════════════

void WindowTrackingService::recordSnapIntent(const QString& windowId, bool wasUserInitiated)
{
    Q_ASSERT(m_snapState);
    if (!m_snapState) {
        return;
    }
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
    Q_ASSERT(m_snapState);
    if (!m_snapState) {
        return;
    }
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

void WindowTrackingService::markAsAutoSnapped(const QString& windowId)
{
    Q_ASSERT(m_snapState);
    if (!m_snapState || windowId.isEmpty()) {
        return;
    }
    m_snapState->markAsAutoSnapped(windowId);
}

bool WindowTrackingService::isAutoSnapped(const QString& windowId) const
{
    Q_ASSERT(m_snapState);
    if (!m_snapState) {
        return false;
    }
    return m_snapState->isAutoSnapped(windowId);
}

bool WindowTrackingService::clearAutoSnapped(const QString& windowId)
{
    Q_ASSERT(m_snapState);
    if (!m_snapState) {
        return false;
    }
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
    const int remaining = it->size();
    if (it->isEmpty()) {
        m_pendingRestoreQueues.erase(it);
    }
    qCDebug(lcPlacement) << "Consumed pending assignment for" << appId << "remaining:" << remaining;
    markDirty(DirtyPendingRestores);
    return true;
}

} // namespace PhosphorPlacement
