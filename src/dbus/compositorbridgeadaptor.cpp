// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "compositorbridgeadaptor.h"
#include "../core/logging.h"

#include <dbus_types.h>
#include <QUuid>

namespace PlasmaZones {

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

    m_bridgeName = compositorName;
    m_bridgeVersion = version;
    m_capabilities = capabilities;

    qCInfo(lcDbusWindow) << "Compositor bridge registered:" << compositorName << version
                         << "capabilities:" << capabilities;

    Q_EMIT bridgeRegistered(compositorName, version, capabilities);

    BridgeRegistrationResult result;
    result.apiVersion = QStringLiteral("1");
    result.bridgeName = compositorName;
    result.sessionId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    return result;
}

void CompositorBridgeAdaptor::reportModifierState(int modifiers, int mouseButtons)
{
    Q_EMIT modifierStateChanged(modifiers, mouseButtons);
}

} // namespace PlasmaZones
