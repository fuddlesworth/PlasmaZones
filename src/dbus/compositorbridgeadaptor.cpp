// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "compositorbridgeadaptor.h"
#include "../core/constants.h"
#include "../core/logging.h"

#include <PhosphorProtocol/ServiceConstants.h>
#include <PhosphorProtocol/BridgeMarshalling.h>
#include <QUuid>

namespace PlasmaZones {

// Protocol version constants are the single source of truth in
// PhosphorProtocol::Service (libs/phosphor-protocol). The effect links
// compositor-common for its own constants; the daemon includes
// PhosphorProtocol/ServiceConstants.h directly.
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

    // Logged only once every rejection gate has passed: a rejected peer
    // replaces nothing, so warning before the gates would mislead.
    if (!m_bridgeName.isEmpty()) {
        qCWarning(lcDbusWindow) << "Compositor bridge re-registration: replacing" << m_bridgeName << m_bridgeVersion
                                << "with" << compositorName << version;
    }

    m_bridgeName = compositorName;
    m_bridgeVersion = version;
    m_capabilities = capabilities;

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

} // namespace PlasmaZones
