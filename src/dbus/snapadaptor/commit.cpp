// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "../snapadaptor.h"
#include "../windowtrackingadaptor.h"
#include "../../core/logging.h"
#include "../../core/windowtrackingservice.h"
#include "../../snap/SnapEngine.h"

namespace PlasmaZones {

// ═══════════════════════════════════════════════════════════════════════════════
// Snap-commit D-Bus slots — thin forwarders over SnapEngine.
//
// The full orchestration (clear floating, clear auto-snapped flag, consume
// pending restore, assign to zone, update last-used tracking, emit state-
// change signal) lives in SnapEngine::commitSnap / commitMultiZoneSnap /
// uncommitSnap. These D-Bus entry points survive as the external contract,
// but their bodies do only two things:
//
//   1. Validate and resolve the screen id
//   2. Forward to the engine
// ═══════════════════════════════════════════════════════════════════════════════

void SnapAdaptor::windowSnapped(const QString& windowId, const QString& zoneId, const QString& screenId)
{
    if (!validateWindowId(windowId, QStringLiteral("track window snap"))) {
        return;
    }
    if (zoneId.isEmpty()) {
        qCWarning(lcDbusWindow) << "Window snap: cannot track, empty zone ID";
        return;
    }
    if (!m_engine) {
        return;
    }
    const QString resolvedScreen = resolveScreenForSnap(screenId, zoneId);
    m_engine->commitSnap(windowId, zoneId, resolvedScreen);
}

void SnapAdaptor::windowSnappedMultiZone(const QString& windowId, const QStringList& zoneIds, const QString& screenId)
{
    if (!validateWindowId(windowId, QStringLiteral("track multi-zone window snap"))) {
        return;
    }
    if (zoneIds.isEmpty() || zoneIds.first().isEmpty()) {
        qCWarning(lcDbusWindow) << "Multi-zone window snap: cannot track, empty zone IDs";
        return;
    }
    if (!m_engine) {
        return;
    }
    const QString resolvedScreen = resolveScreenForSnap(screenId, zoneIds.first());
    m_engine->commitMultiZoneSnap(windowId, zoneIds, resolvedScreen);
}

void SnapAdaptor::windowUnsnapped(const QString& windowId)
{
    if (!validateWindowId(windowId, QStringLiteral("untrack window"))) {
        return;
    }
    if (!m_engine) {
        return;
    }
    m_engine->uncommitSnap(windowId);
}

void SnapAdaptor::windowsSnappedBatch(const SnapConfirmationList& entries)
{
    qCInfo(lcDbusWindow) << "windowsSnappedBatch: processing" << entries.size() << "entries";

    for (const auto& entry : entries) {
        if (entry.windowId.isEmpty()) {
            continue;
        }

        if (entry.isRestore) {
            // Window's zone exceeded the new layout — unsnap and clear pre-tile geometry
            windowUnsnapped(entry.windowId);
            if (m_adaptor) {
                m_adaptor->clearPreTileGeometry(entry.windowId);
            }
        } else {
            windowSnapped(entry.windowId, entry.zoneId, entry.screenId);
        }
    }
}

void SnapAdaptor::recordSnapIntent(const QString& windowId, bool wasUserInitiated)
{
    if (windowId.isEmpty()) {
        return;
    }
    if (!m_adaptor || !m_adaptor->service()) {
        return;
    }
    m_adaptor->service()->recordSnapIntent(windowId, wasUserInitiated);
}

// ═══════════════════════════════════════════════════════════════════════════════
// Private helpers
// ═══════════════════════════════════════════════════════════════════════════════

bool SnapAdaptor::validateWindowId(const QString& windowId, const QString& operation) const
{
    if (windowId.isEmpty()) {
        qCWarning(lcDbusWindow) << "Cannot" << operation << "- empty window ID";
        return false;
    }
    return true;
}

QString SnapAdaptor::resolveScreenForSnap(const QString& callerScreen, const QString& zoneId) const
{
    if (!m_adaptor) {
        return callerScreen;
    }
    return m_adaptor->resolveScreenForSnap(callerScreen, zoneId);
}

} // namespace PlasmaZones
