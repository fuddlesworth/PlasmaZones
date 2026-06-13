// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorServicePipeWire/phosphorservicepipewire_export.h>
// Full definitions, not forward declarations, for both types this header's
// moc surface exposes: the `connection` Q_PROPERTY carries
// PipeWireConnection* and the nodeAdded/nodeRemoved relay signals carry
// PwNode*. Qt6 moc auto-registers property and signal-parameter metatypes,
// and QMetaType SFINAE-probes completeness — a fwd decl for either would
// re-fire GCC's -Wsfinae-incomplete the moment the mocs_compilation
// aggregation order stops shielding this header (same rationale as
// PwNodeModel.h).
#include <PhosphorServicePipeWire/PipeWireConnection.h>
#include <PhosphorServicePipeWire/PwNode.h>

#include <QObject>
#include <QString>

#include <memory>

namespace PhosphorServicePipeWire {

/// Process-wide PipeWire host: owns the single `PipeWireConnection`
/// instance the shell binds to and re-exposes its observable state as
/// QML-friendly properties, including the `error(QString)` signal.
/// Registered as a QML singleton (see qmlregistration.cpp; the class
/// deliberately omits QML_ELEMENT / QML_SINGLETON macros and is wired
/// imperatively, matching every sibling phosphor-service-* lib) so QML
/// reads `PipeWireHost.connection.defaultSinkName` rather than
/// maintaining its own `PipeWireConnection` instance.
///
/// Facade coverage: every observable signal of `PipeWireConnection`
/// is forwarded one-for-one — `connectedChanged`,
/// `daemonAvailableChanged`, `defaultSinkNameChanged`,
/// `defaultSourceNameChanged`, `error`, `nodeAdded`, `nodeRemoved`.
/// QML consumers can OBSERVE exclusively through `PipeWireHost.*` and
/// never need to reach through `.connection.*` for state. The mutating
/// slots (`setDefaultSink` / `setDefaultSource` / `writeVolumes` /
/// `writeMuted`) live only on the connection, so writers still go
/// through `host.connection.*` — which the `connection` property exposes
/// (alongside its other use: handing the connection to a `PwNodeModel`).
///
/// The host is constructed lazily on first QML use (via
/// `qmlRegisterSingletonType`'s factory) and auto-connects to the
/// daemon. Shells that want explicit lifecycle control can construct
/// their own `PipeWireConnection` directly and skip the singleton, or
/// drive the existing instance through `connectToDaemon()` /
/// `disconnectFromDaemon()` / `reconnect()`.
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
    /// Mirror of the underlying connection's `daemonAvailable`. See
    /// `PipeWireConnection::daemonAvailable` for the full semantics
    /// (info-answered vs. fully connected, and the three reset paths).
    Q_PROPERTY(bool daemonAvailable READ isDaemonAvailable NOTIFY daemonAvailableChanged)
    Q_PROPERTY(QString defaultSinkName READ defaultSinkName NOTIFY defaultSinkNameChanged)
    Q_PROPERTY(QString defaultSourceName READ defaultSourceName NOTIFY defaultSourceNameChanged)

public:
    explicit PipeWireHost(QObject* parent = nullptr);
    ~PipeWireHost() override;

    /// Constructed eagerly in PipeWireHost ctor; guaranteed non-null for
    /// the host's lifetime. CONSTANT semantics in the Q_PROPERTY above
    /// are safe because the pointer never changes after construction —
    /// reconnect / connectToDaemon / disconnectFromDaemon mutate the
    /// underlying connection's state, never its identity.
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
    /// Asynchronously connect the underlying `PipeWireConnection` to
    /// the daemon. Symmetric with `disconnectFromDaemon()`; exposed on
    /// the host (alongside the existing `reconnect()`) so QML has full
    /// lifecycle control without reaching through
    /// `host.connection.connectToDaemon()` — keeps the facade complete.
    void connectToDaemon();
    /// Asynchronously disconnect the underlying `PipeWireConnection`
    /// from the daemon. Symmetric with `connectToDaemon()`; same
    /// rationale — QML shells that want to pause the daemon connection
    /// (battery saver, suspend) can do it through the host facade.
    void disconnectFromDaemon();

Q_SIGNALS:
    void connectedChanged();
    void daemonAvailableChanged();
    void defaultSinkNameChanged();
    void defaultSourceNameChanged();
    /// Forwarded from `PipeWireConnection::error`. Fired from the GUI
    /// thread when PipeWire reports a core-level error (daemon restart,
    /// version mismatch, pre-handshake failure). The host re-emits
    /// without transformation so QML observers can bind to `error` on
    /// the host singleton rather than reaching through `.connection`.
    void error(const QString& message);
    /// Forwarded from `PipeWireConnection::nodeAdded`. Same payload and
    /// semantics — fired from the GUI thread when the registry reports
    /// a new audio node. Exposed on the host so QML observers can wire
    /// `PipeWireHost.onNodeAdded` without reaching through `.connection`.
    /// FQN `PhosphorServicePipeWire::PwNode*` is intentional — moc's
    /// metatype registration matches the signature spelling, and the
    /// FQN dodges QML ambiguity in queued connections.
    void nodeAdded(PhosphorServicePipeWire::PwNode* node);
    /// Forwarded from `PipeWireConnection::nodeRemoved`. Fired BEFORE
    /// the PwNode is destroyed so observers can detach. Same FQN
    /// rationale as `nodeAdded`.
    void nodeRemoved(PhosphorServicePipeWire::PwNode* node);

private:
    class Private;
    std::unique_ptr<Private> d;
};

} // namespace PhosphorServicePipeWire
