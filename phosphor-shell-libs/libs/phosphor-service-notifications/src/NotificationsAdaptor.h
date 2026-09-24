// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once
#include <PhosphorServiceNotifications/NotificationServer.h>
#include <QDBusAbstractAdaptor>
#include <QDBusMessage>

namespace PhosphorServiceNotifications {
// The sender belongs to the transport, never to a caller-provided hint. Keeping
// it here allows private inline replies while retaining the standard interface.
class NotificationsAdaptor : public QDBusAbstractAdaptor
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.freedesktop.Notifications")
public:
    explicit NotificationsAdaptor(NotificationServer* server)
        : QDBusAbstractAdaptor(server)
        , m_server(server)
    {
        setAutoRelaySignals(true);
    }
public Q_SLOTS:
    uint Notify(const QString& app, uint replaces, const QString& icon, const QString& summary, const QString& body,
                const QStringList& actions, const QVariantMap& hints, int timeout, const QDBusMessage& message)
    {
        return m_server->Notify(app, replaces, icon, summary, body, actions, hints, timeout, message.service());
    }
    void CloseNotification(uint id)
    {
        m_server->CloseNotification(id);
    }
    QStringList GetCapabilities()
    {
        return m_server->GetCapabilities();
    }
    QString GetServerInformation(QString& vendor, QString& version, QString& specVersion)
    {
        return m_server->GetServerInformation(vendor, version, specVersion);
    }
Q_SIGNALS:
    void NotificationClosed(uint id, uint reason);
    void ActionInvoked(uint id, const QString& actionKey);
    void ActivationToken(uint id, const QString& token);
    // Introspection only; reply() sends this signal directly to the origin.
    void NotificationReplied(uint id, const QString& text);

private:
    NotificationServer* m_server;
};
}
