// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorServicePipeWire/PipeWireHost.h>

#include <PhosphorServicePipeWire/PipeWireConnection.h>

namespace PhosphorServicePipeWire {

class PipeWireHost::Private
{
public:
    std::unique_ptr<PipeWireConnection> connection;
};

PipeWireHost::PipeWireHost(QObject* parent)
    : QObject(parent)
    , d(std::make_unique<Private>())
{
    d->connection = std::make_unique<PipeWireConnection>(this);
    // Forward every observable signal — including `error` — so QML can
    // bind through PipeWireHost without reaching for `.connection.foo`
    // everywhere. The forwarding is symmetric (no transformation) — the
    // host is a facade, not a controller.
    QObject::connect(d->connection.get(), &PipeWireConnection::connectedChanged, this, &PipeWireHost::connectedChanged);
    QObject::connect(d->connection.get(), &PipeWireConnection::daemonAvailableChanged, this,
                     &PipeWireHost::daemonAvailableChanged);
    QObject::connect(d->connection.get(), &PipeWireConnection::defaultSinkNameChanged, this,
                     &PipeWireHost::defaultSinkNameChanged);
    QObject::connect(d->connection.get(), &PipeWireConnection::defaultSourceNameChanged, this,
                     &PipeWireHost::defaultSourceNameChanged);
    QObject::connect(d->connection.get(), &PipeWireConnection::error, this, &PipeWireHost::error);
    // Kick off the handshake immediately — typical QML usage binds to
    // `PipeWireHost.connection.nodes` and expects state to be live as
    // soon as the singleton is instantiated.
    d->connection->connectToDaemon();
}

PipeWireHost::~PipeWireHost() = default;

PipeWireConnection* PipeWireHost::connection() const
{
    return d->connection.get();
}

bool PipeWireHost::isConnected() const
{
    return d->connection->isConnected();
}

bool PipeWireHost::isDaemonAvailable() const
{
    return d->connection->isDaemonAvailable();
}

QString PipeWireHost::defaultSinkName() const
{
    return d->connection->defaultSinkName();
}

QString PipeWireHost::defaultSourceName() const
{
    return d->connection->defaultSourceName();
}

void PipeWireHost::reconnect()
{
    d->connection->disconnectFromDaemon();
    d->connection->connectToDaemon();
}

void PipeWireHost::connectToDaemon()
{
    d->connection->connectToDaemon();
}

void PipeWireHost::disconnectFromDaemon()
{
    d->connection->disconnectFromDaemon();
}

} // namespace PhosphorServicePipeWire
