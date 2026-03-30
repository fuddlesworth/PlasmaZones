// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "compositorbridgeadaptor.h"
#include "../core/logging.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QUuid>

namespace PlasmaZones {

CompositorBridgeAdaptor::CompositorBridgeAdaptor(QObject* parent)
    : QDBusAbstractAdaptor(parent)
{
}

QString CompositorBridgeAdaptor::registerBridge(const QString& compositorName, const QString& version,
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

    QJsonObject result;
    result[QLatin1String("apiVersion")] = 1;
    result[QLatin1String("bridgeName")] = compositorName;
    result[QLatin1String("sessionId")] = QUuid::createUuid().toString(QUuid::WithoutBraces);
    return QString::fromUtf8(QJsonDocument(result).toJson(QJsonDocument::Compact));
}

void CompositorBridgeAdaptor::reportModifierState(int modifiers, int mouseButtons)
{
    Q_EMIT modifierStateChanged(modifiers, mouseButtons);
}

} // namespace PlasmaZones
