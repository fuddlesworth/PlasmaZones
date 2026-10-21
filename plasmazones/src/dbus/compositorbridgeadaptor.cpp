// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "compositorbridgeadaptor.h"
#include "core/types/constants.h"
#include "core/platform/logging.h"
#include "dbus/dbushelpers.h"

#include <PhosphorProtocol/ServiceConstants.h>
#include <PhosphorProtocol/BridgeMarshalling.h>
#include <QDBusConnectionInterface>
#include <QDBusContext>
#include <QUuid>

namespace PlasmaZones {

// Protocol version constants are the single source of truth in
// PhosphorProtocol::Service (libs/phosphor-protocol). The effect and the
// daemon both read them from PhosphorProtocol/ServiceConstants.h.
namespace {
constexpr int DaemonApiVersion = PhosphorProtocol::Service::ApiVersion;
constexpr int DaemonMinPeerApiVersion = PhosphorProtocol::Service::MinPeerApiVersion;
} // namespace

CompositorBridgeAdaptor::CompositorBridgeAdaptor(QObject* parent)
    : QDBusAbstractAdaptor(parent)
{
}

PhosphorProtocol::BridgeRegistrationResult CompositorBridgeAdaptor::registerBridge(const QString& compositorName,
                                                                                   const QString& version,
                                                                                   const QStringList& capabilities)
{
    // Input validation at the D-Bus boundary: an empty compositorName would
    // commit a registration that isBridgeRegistered() (keyed on a non-empty
    // name) can never observe, silently skipping the re-registration warning
    // on the next register too. Reject before any state changes.
    if (compositorName.isEmpty()) {
        qCWarning(lcDbusWindow) << "Compositor bridge REJECTED: empty compositorName";
        PhosphorProtocol::BridgeRegistrationResult result;
        result.apiVersion = QString::number(DaemonApiVersion);
        result.bridgeName = compositorName;
        result.sessionId = QStringLiteral("REJECTED");
        return result;
    }

    // Version gate: reject effects that speak an older protocol version.
    // The effect passes its apiVersion as the `version` string.
    const int peerApiVersion = version.toInt();
    if (peerApiVersion < DaemonMinPeerApiVersion) {
        qCWarning(lcDbusWindow) << "Compositor bridge REJECTED: peer apiVersion" << peerApiVersion << "is below minimum"
                                << DaemonMinPeerApiVersion << "(compositor:" << compositorName << ")."
                                << "Update the effect to match the daemon.";
        PhosphorProtocol::BridgeRegistrationResult result;
        result.apiVersion = QString::number(DaemonApiVersion);
        result.bridgeName = compositorName;
        result.sessionId = QStringLiteral("REJECTED");
        return result;
    }

    // One bridge at a time. The registered bus name is what every
    // compositor-only method admits (DbusHelpers::admitCompositorReport), so a
    // registration from another peer while the bridge is still on the bus
    // would hand that peer the compositor's methods and lock the real
    // compositor out (#1155). The same name registering again (the effect
    // reloading in a running KWin) and a new name once the old one has left
    // the bus (KWin or the daemon restarted) are let through.
    const QString caller = DbusHelpers::busCaller(this);
    if (!caller.isEmpty() && !m_bridgeService.isEmpty() && caller != m_bridgeService) {
        const QDBusContext* const context = DbusHelpers::busContext(this);
        const QDBusConnectionInterface* const bus = context ? context->connection().interface() : nullptr;
        if (bus && bus->isServiceRegistered(m_bridgeService)) {
            qCWarning(lcDbusWindow) << "Compositor bridge REJECTED:" << caller << "tried to register while"
                                    << m_bridgeService << "is still registered (compositor:" << compositorName << ")";
            PhosphorProtocol::BridgeRegistrationResult result;
            result.apiVersion = QString::number(DaemonApiVersion);
            result.bridgeName = compositorName;
            result.sessionId = QStringLiteral("REJECTED");
            return result;
        }
    }

    // Logged only once every rejection gate has passed: a rejected peer
    // replaces nothing, so warning before the gates would mislead.
    if (!m_bridgeName.isEmpty()) {
        qCWarning(lcDbusWindow) << "Compositor bridge re-registration: replacing" << m_bridgeName << m_bridgeVersion
                                << "with" << compositorName << version;
    }

    m_bridgeName = compositorName;
    m_bridgeVersion = version;
    m_capabilities = capabilities;
    // Remember WHO registered, so a later reportGesture can be attributed.
    // A direct (non-D-Bus) call has no peer and leaves this empty.
    m_bridgeService = caller;

    qCInfo(lcDbusWindow) << "Compositor bridge registered:" << compositorName << "apiVersion=" << version
                         << "capabilities:" << capabilities;

    Q_EMIT bridgeRegistered(compositorName, version, capabilities);

    PhosphorProtocol::BridgeRegistrationResult result;
    result.apiVersion = QString::number(DaemonApiVersion);
    result.bridgeName = compositorName;
    result.sessionId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    return result;
}

void CompositorBridgeAdaptor::reportModifierState(int modifiers, int mouseButtons)
{
    Q_EMIT modifierStateChanged(modifiers, mouseButtons);
}

void CompositorBridgeAdaptor::reportGesture(const QString& kind, const QString& direction, uint fingerCount)
{
    // The interface documents this as firing "only for gestures the bridge
    // registered under the 'gestures' capability", and that gate has to be
    // real: gestureReported drives the shell's launcher and dashboard, and the
    // session bus is an untrusted boundary.
    //
    // The capability list alone is NOT that gate — it is daemon-wide state
    // describing whichever bridge registered last, so any peer would pass it
    // as soon as a gestures-capable bridge existed. The caller's own bus name
    // has to match the registered bridge's. A direct (non-D-Bus) call has no
    // peer to authorise, which is the path the unit tests take.
    if (!isBridgeRegistered() || !hasCapability(QStringLiteral("gestures"))) {
        qCDebug(lcDbusWindow) << "reportGesture dropped: no bridge registered for gestures";
        return;
    }
    // The caller's name is checked by the shared compositor-only gate, which
    // answers a foreign caller with AccessDenied and warns once per caller.
    if (!DbusHelpers::admitCompositorReport(this, QLatin1StringView(__func__))) {
        return;
    }
    // Boundary validation: the vocabulary is closed, so anything else is a
    // bridge bug and is dropped rather than relayed to the shell.
    static const QStringList kSwipeDirections{QStringLiteral("up"), QStringLiteral("down"), QStringLiteral("left"),
                                              QStringLiteral("right")};
    static const QStringList kPinchDirections{QStringLiteral("expanding"), QStringLiteral("contracting")};
    const bool swipe = kind == QLatin1String("swipe") && kSwipeDirections.contains(direction);
    const bool pinch = kind == QLatin1String("pinch") && kPinchDirections.contains(direction);
    if ((!swipe && !pinch) || fingerCount < 1 || fingerCount > 5) {
        qCWarning(lcDbusWindow) << "reportGesture dropped:" << kind << direction << fingerCount;
        return;
    }
    Q_EMIT gestureReported(kind, direction, fingerCount);
}

} // namespace PlasmaZones
