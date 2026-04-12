// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "compositorbridgeadaptor.h"
#include "../core/constants.h"
#include "../core/logging.h"

#include <dbus_types.h>
#include <QUuid>

namespace PlasmaZones {

// Protocol version constants live in core/constants.h and are mirrored in
// compositor-common/dbus_constants.h (the effect's canonical copy). Both
// headers cannot be included from the same TU because they historically
// define the same DBus namespace — when that duplication is cleaned up
// this will collapse to a single source of truth.
namespace {
constexpr int DaemonApiVersion = DBus::ApiVersion;
constexpr int DaemonMinPeerApiVersion = DBus::MinPeerApiVersion;
} // namespace

CompositorBridgeAdaptor::CompositorBridgeAdaptor(QObject* parent)
    : QDBusAbstractAdaptor(parent)
{
}

BridgeRegistrationResult CompositorBridgeAdaptor::registerBridge(const QString& compositorName, const QString& version,
                                                                 const QStringList& capabilities)
{
    if (!m_bridgeName.isEmpty()) {
        qCWarning(lcDbusWindow) << "Compositor bridge re-registration: replacing" << m_bridgeName << m_bridgeVersion
                                << "with" << compositorName << version;
    }

    // Version gate: reject effects that speak an older protocol version.
    // The effect passes its apiVersion as the `version` string.
    const int peerApiVersion = version.toInt();
    if (peerApiVersion < DaemonMinPeerApiVersion) {
        qCWarning(lcDbusWindow) << "Compositor bridge REJECTED: peer apiVersion" << peerApiVersion << "is below minimum"
                                << DaemonMinPeerApiVersion << "(compositor:" << compositorName << ")."
                                << "Update the effect to match the daemon.";
        BridgeRegistrationResult result;
        result.apiVersion = QString::number(DaemonApiVersion);
        result.bridgeName = compositorName;
        result.sessionId = QStringLiteral("REJECTED");
        return result;
    }

    m_bridgeName = compositorName;
    m_bridgeVersion = version;
    m_capabilities = capabilities;

    qCInfo(lcDbusWindow) << "Compositor bridge registered:" << compositorName << "apiVersion=" << version
                         << "capabilities:" << capabilities;

    Q_EMIT bridgeRegistered(compositorName, version, capabilities);

    BridgeRegistrationResult result;
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
