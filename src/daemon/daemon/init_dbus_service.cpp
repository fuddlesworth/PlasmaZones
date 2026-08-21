// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// ═══════════════════════════════════════════════════════════════════════════════
// Daemon — D-Bus service and object registration
//
// Split out of init_engines.cpp, which holds the engine wiring proper. Bus
// registration is its own concern: it runs before the event loop, confirms the
// well-known name main() already claimed, registers the daemon object, and
// wires the zone-detection adaptor's highlight feedback into the overlay.
// ═══════════════════════════════════════════════════════════════════════════════

#include "daemon/daemon.h"

#include "daemon/overlayservice.h"
#include "dbus/zonedetectionadaptor.h"

#include <PhosphorProtocol/ServiceConstants.h>

#include "core/platform/logging.h"

#include <QDBusConnection>
#include <QDBusError>

namespace PlasmaZones {

bool Daemon::registerDBusService()
{
    // Register D-Bus service and object with error handling
    auto bus = QDBusConnection::sessionBus();
    if (!bus.isConnected()) {
        qCCritical(lcDaemon) << "Session D-Bus: cannot connect, daemon cannot function";
        return false;
    }

    // Claim the well-known name. In this process the claim has ALREADY been made:
    // main() registers the name as its single-instance gate, before constructing
    // the Daemon, because that is the only place a duplicate start can exit 0
    // cheaply (exiting non-zero there would put systemd's Restart=on-failure into
    // a loop on every duplicate autostart). registerService on a name the same
    // connection already owns returns true, so this call is the confirmation step,
    // not a second claim.
    //
    // It is kept rather than assumed because init() must not register its object
    // against a name this process does not hold, and because Daemon::stop()
    // releases the name (lifecycle.cpp) — so any future re-init would need the
    // claim to happen here.
    //
    // There is deliberately no retry loop. An earlier version backed off over
    // 300 ms and classified ServiceUnknown/NoReply as transient, but none of that
    // is reachable: main() has already completed a successful round trip to the
    // bus, so by the time init() runs the bus is up and the name is ours. A false
    // here means the ordering above changed, which is a programming error and
    // wants a clear message, not a sleep.
    if (!bus.registerService(QString(PhosphorProtocol::Service::Name))) {
        const QDBusError error = bus.lastError();
        if (error.type() == QDBusError::NoError) {
            qCCritical(lcDaemon) << "Failed to register D-Bus service=" << PhosphorProtocol::Service::Name
                                 << "— the name is owned by another connection. main() claims it before"
                                 << "constructing the Daemon, so reaching this means that gate was bypassed.";
        } else {
            qCCritical(lcDaemon) << "Failed to register D-Bus service=" << PhosphorProtocol::Service::Name
                                 << "error=" << error.message() << "type=" << error.type();
        }
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
