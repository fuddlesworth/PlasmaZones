// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include "PhosphorSettingsUi/DBusBridge.h"

#include <QtDBus/QDBusConnection>
#include <QtDBus/QDBusPendingCall>

namespace PhosphorSettingsUi {

DBusBridge::DBusBridge(DBusEndpoint endpoint, QObject* parent)
    : QObject(parent)
    , m_endpoint(std::move(endpoint))
{
}

DBusBridge::~DBusBridge() = default;

DBusEndpoint DBusBridge::endpoint() const
{
    return m_endpoint;
}

QDBusMessage DBusBridge::call(const QString& method, const QVariantList& args) const
{
    return callOn(m_endpoint.interface, method, args);
}

QDBusMessage DBusBridge::callOn(const QString& interface, const QString& method, const QVariantList& args) const
{
    QDBusMessage msg = QDBusMessage::createMethodCall(m_endpoint.service, m_endpoint.objectPath, interface, method);
    if (!args.isEmpty()) {
        msg.setArguments(args);
    }
    return QDBusConnection::sessionBus().call(msg, QDBus::Block, m_endpoint.syncTimeoutMs);
}

void DBusBridge::asyncCall(const QString& method, const QVariantList& args) const
{
    asyncCallOn(m_endpoint.interface, method, args);
}

void DBusBridge::asyncCallOn(const QString& interface, const QString& method, const QVariantList& args) const
{
    QDBusMessage msg = QDBusMessage::createMethodCall(m_endpoint.service, m_endpoint.objectPath, interface, method);
    if (!args.isEmpty()) {
        msg.setArguments(args);
    }
    QDBusConnection::sessionBus().asyncCall(msg);
}

} // namespace PhosphorSettingsUi
