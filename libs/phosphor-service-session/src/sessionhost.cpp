// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorServiceSession/SessionHost.h>

#include <QDBusConnection>

#include <utility>

namespace PhosphorServiceSession {

class SessionHost::Private
{
public:
    Private(QDBusConnection connection, QString service)
        : bus(std::move(connection))
        , service(std::move(service))
    {
    }

    // The system-bus connection and the logind well-known name. Borrowed by the
    // capability / action / inhibitor machinery added in milestones 2-5.
    QDBusConnection bus;
    QString service;
};

SessionHost::SessionHost(QObject* parent)
    : SessionHost(QDBusConnection::systemBus(), QStringLiteral("org.freedesktop.login1"), parent)
{
}

SessionHost::SessionHost(QDBusConnection connection, QString service, QObject* parent)
    : QObject(parent)
    , d(std::make_unique<Private>(std::move(connection), std::move(service)))
{
}

SessionHost::~SessionHost() = default;

} // namespace PhosphorServiceSession
