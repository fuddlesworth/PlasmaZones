// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorServicePipeWire/phosphorservicepipewire_export.h>

#include <QObject>
#include <QString>

#include <memory>

namespace PhosphorServicePipeWire {

class PipeWireConnection;

/// Process-wide PipeWire host: owns the single `PipeWireConnection`
/// instance the shell binds to and re-exposes its observable state as
/// QML-friendly properties. Registered as a QML singleton (see
/// qmlregistration.cpp; the class deliberately omits QML_ELEMENT /
/// QML_SINGLETON macros and is wired imperatively, matching every
/// sibling phosphor-service-* lib) so QML reads
/// `PipeWireHost.connection.defaultSinkName` rather than maintaining
/// its own `PipeWireConnection` instance.
///
/// The host is constructed lazily on first QML use (via
/// `qmlRegisterSingletonType`'s factory) and auto-connects to the
/// daemon. Shells that want explicit lifecycle control can construct
/// their own `PipeWireConnection` directly and skip the singleton.
///
/// Why a separate host? The PipeWire loop thread is expensive enough
/// (one extra thread per process) that we don't want to spin one up
/// per QML scope. Forcing all QML consumers through this singleton
/// guarantees they share the same loop + registry walk.
class PHOSPHORSERVICEPIPEWIRE_EXPORT PipeWireHost : public QObject
{
    Q_OBJECT
    Q_DISABLE_COPY_MOVE(PipeWireHost)
    Q_PROPERTY(PhosphorServicePipeWire::PipeWireConnection* connection READ connection CONSTANT)
    Q_PROPERTY(bool connected READ isConnected NOTIFY connectedChanged)
    Q_PROPERTY(bool daemonAvailable READ isDaemonAvailable NOTIFY daemonAvailableChanged)
    Q_PROPERTY(QString defaultSinkName READ defaultSinkName NOTIFY defaultSinkNameChanged)
    Q_PROPERTY(QString defaultSourceName READ defaultSourceName NOTIFY defaultSourceNameChanged)

public:
    explicit PipeWireHost(QObject* parent = nullptr);
    ~PipeWireHost() override;

    [[nodiscard]] PipeWireConnection* connection() const;
    [[nodiscard]] bool isConnected() const;
    [[nodiscard]] bool isDaemonAvailable() const;
    [[nodiscard]] QString defaultSinkName() const;
    [[nodiscard]] QString defaultSourceName() const;

public Q_SLOTS:
    /// Trigger a re-connect on the underlying connection. Useful after
    /// the daemon was offline at startup and the shell wants to retry
    /// without restarting the process.
    void reconnect();

Q_SIGNALS:
    void connectedChanged();
    void daemonAvailableChanged();
    void defaultSinkNameChanged();
    void defaultSourceNameChanged();

private:
    class Private;
    std::unique_ptr<Private> d;
};

} // namespace PhosphorServicePipeWire
