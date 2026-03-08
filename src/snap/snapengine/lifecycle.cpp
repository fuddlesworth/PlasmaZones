// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "../SnapEngine.h"
#include "core/geometryutils.h"
#include "core/interfaces.h"
#include "core/logging.h"
#include "core/virtualdesktopmanager.h"
#include "core/windowtrackingservice.h"

namespace PlasmaZones {

// ═══════════════════════════════════════════════════════════════════════════════
// windowOpened — delegates to resolveWindowRestore() and applies the result
// ═══════════════════════════════════════════════════════════════════════════════

void SnapEngine::windowOpened(const QString& windowId, const QString& screenName, int minWidth, int minHeight)
{
    Q_UNUSED(minWidth)
    Q_UNUSED(minHeight)

    if (windowId.isEmpty() || screenName.isEmpty()) {
        return;
    }

    // Guard: skip if already snapped (prevents double-assignment when both
    // windowOpened and the WTA D-Bus resolveWindowRestore path run for the
    // same window — e.g., effect calls windowOpened then D-Bus resolveWindowRestore).
    if (m_windowTracker->isWindowSnapped(windowId)) {
        qCDebug(lcCore) << "SnapEngine::windowOpened: window" << windowId << "already snapped, skipping";
        return;
    }

    SnapResult result = resolveWindowRestore(windowId, screenName, false);
    if (!result.shouldSnap) {
        return;
    }

    // Apply: mark auto-snapped, clear floating state, assign to zone(s)
    m_windowTracker->markAsAutoSnapped(windowId);
    clearFloatingStateForSnap(windowId, result.screenName);
    assignToZones(windowId, result.zoneIds.isEmpty() ? QStringList{result.zoneId} : result.zoneIds, result.screenName);

    // Emit geometry for KWin effect to apply
    Q_EMIT applyGeometryRequested(windowId, GeometryUtils::rectToJson(result.geometry), result.zoneId,
                                  result.screenName);

    qCInfo(lcCore) << "SnapEngine::windowOpened: snapped" << windowId << "to zone" << result.zoneId << "on"
                   << result.screenName;
}

// ═══════════════════════════════════════════════════════════════════════════════
// resolveWindowRestore — 4-level auto-snap fallback chain
//
// Mostly decision logic: returns a SnapResult for the caller to apply geometry.
// Side effects: consumePendingAssignment (step 2), navigationFeedback emit
// (floating windows). The caller (windowOpened or WTA D-Bus facade) handles
// zone assignment and geometry application.
// ═══════════════════════════════════════════════════════════════════════════════

SnapResult SnapEngine::resolveWindowRestore(const QString& windowId, const QString& screenName, bool sticky)
{
    if (windowId.isEmpty() || screenName.isEmpty()) {
        return SnapResult::noSnap();
    }

    // 0. Floating windows should not be auto-snapped — emit OSD feedback
    if (m_windowTracker->isWindowFloating(windowId)) {
        qCInfo(lcCore) << "resolveWindowRestore: window" << windowId << "is floating — skipping snap";
        Q_EMIT navigationFeedback(true, QStringLiteral("float"), QStringLiteral("floated"), QString(), QString(),
                                  screenName);
        return SnapResult::noSnap();
    }

    // 1. App rules (highest priority)
    {
        SnapResult result = m_windowTracker->calculateSnapToAppRule(windowId, screenName, sticky);
        if (result.shouldSnap) {
            qCInfo(lcCore) << "resolveWindowRestore: appRule matched for" << windowId << "zone=" << result.zoneId;
            return result;
        }
    }

    // 2. Persisted zone (session restore)
    if (m_settings && m_settings->restoreWindowsToZonesOnLogin()) {
        SnapResult result = m_windowTracker->calculateRestoreFromSession(windowId, screenName, sticky);
        if (result.shouldSnap) {
            m_windowTracker->consumePendingAssignment(windowId);
            qCInfo(lcCore) << "resolveWindowRestore: persisted matched for" << windowId << "zone=" << result.zoneId;
            return result;
        }
    }

    // 3. Auto-assign to empty zone
    {
        SnapResult result = m_windowTracker->calculateSnapToEmptyZone(windowId, screenName, sticky);
        if (result.shouldSnap) {
            qCInfo(lcCore) << "resolveWindowRestore: emptyZone matched for" << windowId << "zone=" << result.zoneId;
            return result;
        }
    }

    // 4. Snap to last zone (final fallback)
    {
        SnapResult result = m_windowTracker->calculateSnapToLastZone(windowId, screenName, sticky);
        if (result.shouldSnap) {
            qCInfo(lcCore) << "resolveWindowRestore: lastZone matched for" << windowId << "zone=" << result.zoneId;
            return result;
        }
    }

    return SnapResult::noSnap();
}

} // namespace PlasmaZones
