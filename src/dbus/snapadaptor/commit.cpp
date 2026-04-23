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
// Snap-mode convenience D-Bus slots
//
// Moved from WindowTrackingAdaptor::convenience.cpp. These only call
// SnapEngine and emit signals through the WTA relay (applyGeometryRequested).
// ═══════════════════════════════════════════════════════════════════════════════

void SnapAdaptor::moveWindowToZone(const QString& windowId, const QString& zoneId)
{
    if (!validateWindowId(windowId, QStringLiteral("moveWindowToZone"))) {
        return;
    }

    if (zoneId.isEmpty()) {
        qCWarning(lcDbusWindow) << "moveWindowToZone: empty zone ID";
        return;
    }

    if (!m_adaptor || !m_adaptor->service() || !m_engine) {
        return;
    }

    // Resolve screen for the target zone
    QString screenId = resolveScreenForSnap(QString(), zoneId);

    // Get zone geometry
    QRect geo = m_adaptor->service()->zoneGeometry(zoneId, screenId);
    if (!geo.isValid()) {
        qCWarning(lcDbusWindow) << "moveWindowToZone: invalid geometry for zone:" << zoneId;
        return;
    }

    // Perform snap bookkeeping via SnapEngine
    m_engine->commitSnap(windowId, zoneId, screenId);
    m_adaptor->service()->recordSnapIntent(windowId, true);

    // Request compositor to apply geometry
    Q_EMIT m_adaptor->applyGeometryRequested(windowId, geo.x(), geo.y(), geo.width(), geo.height(), zoneId, screenId,
                                             false);

    qCInfo(lcDbusWindow) << "moveWindowToZone:" << windowId << "-> zone" << zoneId << "on screen" << screenId;
}

void SnapAdaptor::swapWindowsById(const QString& windowId1, const QString& windowId2)
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

    if (!m_adaptor || !m_adaptor->service() || !m_engine) {
        return;
    }

    auto* svc = m_adaptor->service();

    // Get each window's current zone
    QString zoneId1 = svc->zoneForWindow(windowId1);
    QString zoneId2 = svc->zoneForWindow(windowId2);

    if (zoneId1.isEmpty() || zoneId2.isEmpty()) {
        qCWarning(lcDbusWindow) << "swapWindowsById: one or both windows not snapped"
                                << "w1:" << windowId1 << "zone:" << zoneId1 << "w2:" << windowId2 << "zone:" << zoneId2;
        return;
    }

    // Get screens for each window
    QString screen1 = svc->screenAssignments().value(windowId1);
    QString screen2 = svc->screenAssignments().value(windowId2);

    // Get the OTHER window's zone geometry (for the swap)
    QRect geo1 = svc->zoneGeometry(zoneId2, screen2); // window1 moves to zone2
    QRect geo2 = svc->zoneGeometry(zoneId1, screen1); // window2 moves to zone1

    if (!geo1.isValid() || !geo2.isValid()) {
        qCWarning(lcDbusWindow) << "swapWindowsById: invalid geometry for swap";
        return;
    }

    // Update bookkeeping: window1 goes to zone2, window2 goes to zone1
    m_engine->commitSnap(windowId1, zoneId2, screen2);
    m_engine->commitSnap(windowId2, zoneId1, screen1);

    // Emit geometry requests for both
    Q_EMIT m_adaptor->applyGeometryRequested(windowId1, geo1.x(), geo1.y(), geo1.width(), geo1.height(), zoneId2,
                                             screen2, false);
    Q_EMIT m_adaptor->applyGeometryRequested(windowId2, geo2.x(), geo2.y(), geo2.width(), geo2.height(), zoneId1,
                                             screen1, false);

    qCInfo(lcDbusWindow) << "swapWindowsById:" << windowId1 << "<->" << windowId2 << "zones:" << zoneId1 << "<->"
                         << zoneId2;
}

// ═══════════════════════════════════════════════════════════════════════════════
// Snap-mode float D-Bus slots
//
// Moved from WindowTrackingAdaptor::float.cpp. These call SnapEngine
// float methods (snap-mode only — cross-mode routing remains on WTA
// via setWindowFloatingForScreen / toggleFloatForWindow which route
// to EITHER autotile or snap engine).
// ═══════════════════════════════════════════════════════════════════════════════

void SnapAdaptor::toggleFloatForWindow(const QString& windowId, const QString& screenId)
{
    qCInfo(lcDbusWindow) << "toggleFloatForWindow: windowId=" << windowId << "screen=" << screenId;

    if (!validateWindowId(windowId, QStringLiteral("toggle float"))) {
        if (m_adaptor) {
            Q_EMIT m_adaptor->navigationFeedback(false, QStringLiteral("float"), QStringLiteral("invalid_window"),
                                                 QString(), QString(), screenId);
        }
        return;
    }

    if (m_engine) {
        m_engine->toggleWindowFloat(windowId, screenId);
    }
}

void SnapAdaptor::setWindowFloat(const QString& windowId, bool floating)
{
    if (!validateWindowId(windowId, QStringLiteral("set snap float"))) {
        return;
    }

    if (m_engine) {
        m_engine->setWindowFloat(windowId, floating);
    }
}

PlasmaZones::UnfloatRestoreResult SnapAdaptor::calculateUnfloatRestore(const QString& windowId, const QString& screenId)
{
    if (windowId.isEmpty()) {
        return UnfloatRestoreResult{};
    }

    if (!m_engine) {
        return UnfloatRestoreResult{};
    }

    UnfloatResult unfloat = m_engine->resolveUnfloatGeometry(windowId, screenId);
    if (!unfloat.found) {
        qCDebug(lcDbusWindow) << "calculateUnfloatRestore: no restore target for" << windowId;
        return UnfloatRestoreResult{};
    }

    qCDebug(lcDbusWindow) << "calculateUnfloatRestore for" << windowId << "-> zones:" << unfloat.zoneIds
                          << "geo:" << unfloat.geometry;
    return UnfloatRestoreResult{
        true,
        unfloat.zoneIds,
        unfloat.screenId,
        unfloat.geometry.x(),
        unfloat.geometry.y(),
        unfloat.geometry.width(),
        unfloat.geometry.height(),
    };
}

void SnapAdaptor::windowUnsnappedForFloat(const QString& windowId)
{
    if (!validateWindowId(windowId, QStringLiteral("prepare float"))) {
        return;
    }

    if (!m_adaptor || !m_adaptor->service()) {
        return;
    }

    QString previousZoneId = m_adaptor->service()->zoneForWindow(windowId);
    if (previousZoneId.isEmpty()) {
        // Window was not snapped - no-op
        qCDebug(lcDbusWindow) << "windowUnsnappedForFloat: window not in any zone:" << windowId;
        return;
    }

    // Delegate to service
    m_adaptor->service()->unsnapForFloat(windowId);

    qCInfo(lcDbusWindow) << "Window" << windowId << "unsnapped for float from zone" << previousZoneId;
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
