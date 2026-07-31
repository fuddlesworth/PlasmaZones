// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// ═══════════════════════════════════════════════════════════════════════════════
// Daemon — D-Bus service and object registration
//
// Split out of init_engines.cpp, which holds the engine wiring proper. Bus
// registration is its own concern: it runs before the event loop, owns the
// bounded synchronous retry, and wires the two overlay-facing adaptors.
// ═══════════════════════════════════════════════════════════════════════════════

#include "daemon/daemon.h"

#include "daemon/overlayservice.h"
#include "dbus/overlayadaptor.h"
#include "dbus/zonedetectionadaptor.h"

#include <PhosphorProtocol/ServiceConstants.h>

#include "core/platform/logging.h"

#include <QDBusConnection>
#include <QDBusError>
#include <QThread>

namespace PlasmaZones {

bool Daemon::registerDBusService()
{
    // Register D-Bus service and object with error handling and retry logic
    auto bus = QDBusConnection::sessionBus();
    if (!bus.isConnected()) {
        qCCritical(lcDaemon) << "Session D-Bus: cannot connect, daemon cannot function";
        return false;
    }

    // Retry D-Bus service registration with exponential backoff.
    // Synchronous retry is required here because init() runs before QGuiApplication::exec(),
    // so QTimer-based async approaches won't fire. Delays are kept short (300ms total max).
    constexpr int maxRetries = 3;
    constexpr int baseDelayMs = 100; // backoff sleeps 100ms then 200ms
    // Worst-case blocking: 100 + 200 = 300 ms on the GUI thread. The third
    // attempt does not sleep — the `attempt < maxRetries - 1` gate below skips
    // the final (would-be 400ms) delay and returns instead.
    // init() runs before QGuiApplication::exec(), so QTimer-based async
    // approaches don't fire — synchronous sleep is the only retry path
    // available here. The retry is bounded by `maxRetries`, and a bus
    // disconnect during the wait would render every subsequent retry
    // pointless (lastError type stays ServiceUnknown but the actual
    // problem is connection-level).
    bool serviceRegistered = false;
    for (int attempt = 0; attempt < maxRetries; ++attempt) {
        if (!bus.isConnected()) {
            qCCritical(lcDaemon) << "D-Bus bus connection lost mid-retry — aborting service registration";
            return false;
        }
        if (bus.registerService(QString(PhosphorProtocol::Service::Name))) {
            serviceRegistered = true;
            break;
        }

        QDBusError error = bus.lastError();
        if (error.type() == QDBusError::ServiceUnknown || error.type() == QDBusError::NoReply) {
            // Transient error - retry with exponential backoff
            if (attempt < maxRetries - 1) {
                const int delayMs = baseDelayMs * (1 << attempt);
                qCWarning(lcDaemon) << "D-Bus service registration: failed (attempt" << (attempt + 1) << "/"
                                    << maxRetries << ")," << error.message() << "retrying in" << delayMs << "ms";
                QThread::msleep(delayMs);
                continue;
            }
        }

        // Non-retryable error or max retries reached
        qCCritical(lcDaemon) << "Failed to register D-Bus service=" << PhosphorProtocol::Service::Name
                             << "error=" << error.message() << "type=" << error.type();
        return false;
    }

    if (!serviceRegistered) {
        qCCritical(lcDaemon) << "Failed to register D-Bus service after" << maxRetries << "attempts";
        return false;
    }

    // Register D-Bus object (no retry needed - service is already registered)
    if (!bus.registerObject(QString(PhosphorProtocol::Service::ObjectPath), this)) {
        QDBusError error = bus.lastError();
        qCCritical(lcDaemon) << "Failed to register D-Bus object=" << PhosphorProtocol::Service::ObjectPath
                             << "error=" << error.message();
        // Cleanup: unregister service if object registration fails
        bus.unregisterService(QString(PhosphorProtocol::Service::Name));
        return false;
    }

    qCInfo(lcDaemon) << "D-Bus service registered service=" << PhosphorProtocol::Service::Name
                     << "path=" << PhosphorProtocol::Service::ObjectPath;

    // Connect overlay adaptor signals to daemon overlay control
    // Disconnect-first on both: the two adaptors are created in
    // initCoreAdaptors and survive a stop() -> init() cycle, so bare
    // connects here would stack duplicate show/hide + updateGeometries
    // handlers per cycle (same rationale as the rulesChanged sweep).
    disconnect(m_overlayAdaptor, &OverlayAdaptor::overlayVisibilityChanged, this, nullptr);
    connect(m_overlayAdaptor, &OverlayAdaptor::overlayVisibilityChanged, this, [this](bool visible) {
        if (visible) {
            showOverlay();
        } else {
            hideOverlay();
        }
    });

    // Connect zone detection to overlay updates
    disconnect(m_zoneDetectionAdaptor, &ZoneDetectionAdaptor::zoneDetected, this, nullptr);
    connect(m_zoneDetectionAdaptor, &ZoneDetectionAdaptor::zoneDetected, this,
            [this](const QString& zoneId, const PhosphorProtocol::ZoneGeometryRect& geometry) {
                Q_UNUSED(zoneId)
                Q_UNUSED(geometry)
                // Update overlay when zone is detected
                m_overlayService->updateGeometries();
            });

    return true;
}

} // namespace PlasmaZones
