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
    // PipeWireConnection ownership is unique_ptr-only (no Qt parent).
    // Passing `this` as the QObject parent would double up ownership:
    // the unique_ptr inside Private would delete it on host teardown
    // AND Qt would have the host's child-deletion sweep target the
    // same pointer. Per CLAUDE.md the rule is "Parent-based ownership
    // for QObjects; unique_ptr/QPointer otherwise; never manual
    // delete" — pick one model. The connection isn't surfaced via
    // QObject::children() anywhere (we hand it out by raw pointer
    // through `connection()`), so dropping the parent has no
    // observable effect for consumers.
    d->connection = std::make_unique<PipeWireConnection>(nullptr);
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

// Default dtor: ~PipeWireConnection (invoked when Private unwinds via
// the unique_ptr) handles the disconnect contract — it calls
// pw_main_loop_quit and the loop thread's run() invokes doDisconnect on
// exit. Adding an explicit disconnectFromDaemon() here would only
// double up that teardown.
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
