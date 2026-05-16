// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorDBus/Client.h>

#include <PhosphorDBus/Logging.h>

#include <QDBusPendingCallWatcher>
#include <QObject>

namespace PhosphorDBus {

Client::Client(QDBusConnection connection, QString service, QString objectPath, const QLoggingCategory* log)
    : m_connection(std::move(connection))
    , m_service(std::move(service))
    , m_objectPath(std::move(objectPath))
    , m_log(log ? log : &lcPhosphorDBus())
{
}

QDBusMessage Client::createCall(const QString& interface, const QString& method, const QVariantList& args) const
{
    QDBusMessage msg = QDBusMessage::createMethodCall(m_service, m_objectPath, interface, method);
    msg.setArguments(args);
    return msg;
}

void Client::fireAndForget(QObject* parent, const QString& interface, const QString& method, const QVariantList& args,
                           const QString& logContext) const
{
    const QString ctx = logContext.isEmpty() ? method : logContext;
    if (!parent) {
        qCWarning(*m_log) << ctx << "fireAndForget called with null parent, ignoring";
        return;
    }
    QDBusPendingCall pending = m_connection.asyncCall(createCall(interface, method, args));
    auto* watcher = new QDBusPendingCallWatcher(pending, parent);
    const QLoggingCategory* log = m_log;
    QObject::connect(watcher, &QDBusPendingCallWatcher::finished, parent, [ctx, log](QDBusPendingCallWatcher* w) {
        if (w->isError()) {
            qCWarning(*log) << ctx << "D-Bus call failed:" << w->error().message();
        }
        w->deleteLater();
    });
}

void Client::sendOneWay(const QString& interface, const QString& method, const QVariantList& args) const
{
    m_connection.send(createCall(interface, method, args));
}

QDBusPendingCall Client::asyncCall(const QString& interface, const QString& method, const QVariantList& args) const
{
    return m_connection.asyncCall(createCall(interface, method, args));
}

QDBusMessage Client::syncCall(const QString& interface, const QString& method, const QVariantList& args,
                              int timeoutMs) const
{
    return m_connection.call(createCall(interface, method, args), QDBus::Block, timeoutMs);
}

} // namespace PhosphorDBus
