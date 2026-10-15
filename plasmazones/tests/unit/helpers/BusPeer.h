// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QDBusConnection>
#include <QDBusContext>
#include <QDBusError>
#include <QDBusMessage>
#include <QDBusPendingCall>
#include <QObject>
#include <QString>
#include <QTest>
#include <QVariantList>

namespace PlasmaZones::TestHelpers {

/**
 * @brief The object an adaptor is exported through, as Daemon is.
 *
 * Qt sets a bus call's QDBusContext on it, not on the adaptor
 * (DbusHelpers::busContext), so an adaptor parented to a plain QObject sees
 * every bus call as a direct one. A target that uses it lists this header as a
 * source, which is what makes AUTOMOC generate its metaobject.
 */
class BusHost : public QObject, public QDBusContext
{
    Q_OBJECT
};

/**
 * @brief Another process on the session bus, calling an exported host.
 *
 * Exports @p host at /Test<name> on the session bus (the private test bus
 * under ctest) and opens a second connection to that bus, which calls it the
 * way any other client would. The export and the connection go with it.
 */
class BusPeer
{
public:
    BusPeer(QObject* host, const QString& name)
        : m_path(QStringLiteral("/Test") + name)
        , m_peerName(name + QStringLiteral("-peer"))
        , m_registered(m_bus.registerObject(m_path, host))
        , m_peer(QDBusConnection::connectToBus(QDBusConnection::SessionBus, m_peerName))
    {
    }
    ~BusPeer()
    {
        m_bus.unregisterObject(m_path);
        leave();
    }
    BusPeer(const BusPeer&) = delete;
    BusPeer& operator=(const BusPeer&) = delete;

    bool ready() const
    {
        return m_bus.isConnected() && m_registered && m_peer.isConnected();
    }

    /// The peer's unique bus name, the sender the host sees.
    QString service() const
    {
        return m_peer.baseService();
    }

    /// Calls @p method of @p interface on the host and waits for the answer:
    /// the reply, an error reply, or an invalid message when none came within
    /// @p timeoutMs. An empty @p interface sends a call naming none.
    QDBusMessage call(const QString& interface, const QString& method, const QVariantList& args = {},
                      int timeoutMs = 5000)
    {
        QDBusMessage message = QDBusMessage::createMethodCall(m_bus.baseService(), m_path, interface, method);
        message.setArguments(args);
        const QDBusPendingCall pending = m_peer.asyncCall(message, timeoutMs);
        const bool answered = QTest::qWaitFor(
            [&pending] {
                return pending.isFinished();
            },
            timeoutMs + 1000);
        return answered ? pending.reply() : QDBusMessage();
    }

    /// Drops the peer's connection, as a process leaving the bus does. The
    /// connection closes once no QDBusConnection refers to it, so this one
    /// lets go of it first.
    void leave()
    {
        m_peer = QDBusConnection(QString());
        QDBusConnection::disconnectFromBus(m_peerName);
    }

private:
    QDBusConnection m_bus = QDBusConnection::sessionBus();
    const QString m_path;
    const QString m_peerName;
    const bool m_registered;
    QDBusConnection m_peer;
};

/// True when @p reply is an error reply of @p type.
inline bool isBusError(const QDBusMessage& reply, QDBusError::ErrorType type)
{
    return reply.type() == QDBusMessage::ErrorMessage && reply.errorName() == QDBusError::errorString(type);
}

} // namespace PlasmaZones::TestHelpers
