// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// ═══════════════════════════════════════════════════════════════════════════════
// Daemon — D-Bus service and object registration
//
// Split out of init_engines.cpp, which holds the engine wiring proper. Bus
// registration is its own concern: it runs before the event loop, owns the
// bounded synchronous retry, and wires the zone-detection adaptor's
// highlight feedback into the overlay.
// ═══════════════════════════════════════════════════════════════════════════════

#include "daemon/daemon.h"

#include "daemon/overlayservice.h"
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

        // Non-retryable error or max retries reached. registerService reports
        // an already-owned name as a plain false with NO error set, so the
        // generic line below would print an empty message and type 0 for by
        // far the most common cause — another daemon instance is running.
        // Name it.
        if (error.type() == QDBusError::NoError) {
            qCCritical(lcDaemon) << "Failed to register D-Bus service=" << PhosphorProtocol::Service::Name
                                 << "— the name is already owned, most likely by another running PlasmaZones daemon";
            return false;
        }
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

    // OverlayAdaptor::overlayVisibilityChanged is OUTBOUND ONLY: the adaptor
    // relays OverlayService::visibilityChanged onto the bus for clients. The
    // daemon deliberately does NOT subscribe to it. It used to, routing the
    // signal straight back into showOverlay/hideOverlay, so every internal
    // show or hide re-entered the service that had just emitted it: an
    // unguarded feedback cycle. It was asymmetric too — showOverlay refuses
    // when no screen is in snap mode, but hideOverlay always runs
    // clearHighlight(), so an internal hide cleared highlights through a
    // path no caller asked for. Inbound overlay control comes from the
    // adaptor's own D-Bus methods; there is nothing to wire here.
    //
    // No disconnect-first below: initCoreAdaptors' delete preamble
    // (init_adaptors.cpp) tears down the WHOLE previous adaptor set,
    // m_overlayAdaptor and m_zoneDetectionAdaptor included, and re-news both
    // before this function runs — so a stop() -> init() cycle hands us fresh
    // objects with no connections to sweep.

    // Highlight the detected zone. Narrow on purpose: this fires per drag
    // poll, and the old full updateGeometries() rebuilt every screen's
    // overlay geometry each time to answer a question about one zone.
    //
    // highlightZone is SINGLE-zone: it also resets highlightedZoneIds, so it
    // collapses a multi-zone (zone-span) highlight to one zone. Its only
    // trigger is DetectZoneAtPosition, an externally-callable D-Bus method
    // with no in-repo caller — the daemon's own drag path writes highlights
    // directly and never routes through here. An external client polling it
    // DURING a zone-span drag would clobber that drag's multi-highlight;
    // accepted, since the two uses cannot both win and the drag path repaints
    // on its next poll.
    connect(m_zoneDetectionAdaptor, &ZoneDetectionAdaptor::zoneDetected, this,
            [this](const QString& zoneId, const PhosphorProtocol::ZoneGeometryRect& geometry) {
                Q_UNUSED(geometry)
                m_overlayService->highlightZone(zoneId);
            });

    return true;
}

} // namespace PlasmaZones
