// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorServicePipeWire/phosphorservicepipewire_export.h>

#include <QList>
#include <QObject>
#include <QString>

#include <memory>

namespace PhosphorServicePipeWire {

class PwNode;

/// Owns the PipeWire main loop on a dedicated `QThread` and exposes
/// connection state to the GUI thread via Q_PROPERTY-backed signals.
///
/// PipeWire's `pw_main_loop` is not a Qt event loop: every PipeWire
/// API call must execute on the loop's thread. This class hides the
/// thread + loop ownership behind a pimpl, posts loop work via
/// `pw_loop_invoke`, and routes every loop-side event back to the GUI
/// thread through queued signals. Consumers see a single-threaded API
/// that follows Qt's normal "create on the GUI thread, observe via
/// signals" idiom.
///
/// Lifecycle:
/// - Construction starts the loop thread but does NOT connect to the
///   daemon. Call `connect()` to begin the handshake. `connected` flips
///   to true when `pw_core` reports the `done` event for the initial
///   sync, signalling that PipeWire acknowledged us.
/// - `disconnect()` tears down `pw_core` + `pw_context` and pauses the
///   loop. The thread itself stays alive; a subsequent `connect()`
///   re-creates the context cheaply.
/// - Destruction quits the loop, joins the thread, and frees all
///   PipeWire state in the correct order (core → context → loop).
///
/// Error handling: PipeWire core errors (daemon restart, version
/// mismatch, transport drop) fire `error(message)` from the GUI thread
/// and flip `connected` back to false. The class does NOT auto-retry;
/// shells observe the error, decide on a backoff, and call `connect()`
/// again. This keeps the policy in the shell rather than baked into the
/// library, mirroring how `phosphor-service-sni`'s host treats the
/// watcher-respawn case.
class PHOSPHORSERVICEPIPEWIRE_EXPORT PipeWireConnection : public QObject
{
    Q_OBJECT
    Q_DISABLE_COPY_MOVE(PipeWireConnection)
    Q_PROPERTY(bool connected READ isConnected NOTIFY connectedChanged)
    /// True when the PipeWire daemon was reachable at the last
    /// `connect()` attempt. Distinct from `connected`: a daemon may
    /// exist on the bus but reject the handshake (version mismatch,
    /// permission denial). QML uses `daemonAvailable` to drive the
    /// "PipeWire is installed but unreachable" diagnostic.
    Q_PROPERTY(bool daemonAvailable READ isDaemonAvailable NOTIFY daemonAvailableChanged)

public:
    explicit PipeWireConnection(QObject* parent = nullptr);
    ~PipeWireConnection() override;

    /// Asynchronously write a per-channel volume array to the node
    /// with the given PipeWire global id. The write is dispatched onto
    /// the loop thread; consumers observe completion via the node's
    /// `propsChanged` signal once PipeWire echoes the updated Props
    /// pod. Called by `PwNode::setVolumes` / `setVolume`; rarely
    /// useful directly.
    void writeVolumes(quint32 nodeId, const QList<qreal>& volumes);
    /// Asynchronously write a mute state. Same dispatch + echo model
    /// as `writeVolumes`.
    void writeMuted(quint32 nodeId, bool muted);

    [[nodiscard]] bool isConnected() const;
    [[nodiscard]] bool isDaemonAvailable() const;
    /// Snapshot of every node the registry has reported so far. Live
    /// pointers — they're owned by this connection (QObject parent
    /// chain) and survive until the corresponding `nodeRemoved` signal
    /// fires. Returned by value so QML / C++ callers can iterate
    /// without locking; the list itself is mutated only on the GUI
    /// thread.
    [[nodiscard]] QList<PwNode*> nodes() const;

public Q_SLOTS:
    /// Asynchronously establish a `pw_context` + `pw_core` and complete
    /// the initial sync. Safe to call from the GUI thread at any time;
    /// re-issuing while already connected is a no-op.
    void connect();
    /// Asynchronously tear down `pw_core` + `pw_context`. The loop
    /// thread stays alive so a subsequent `connect()` is cheap. Safe to
    /// call from the GUI thread at any time.
    void disconnect();

Q_SIGNALS:
    void connectedChanged();
    void daemonAvailableChanged();
    /// Fired from the GUI thread when PipeWire reports a core-level
    /// error. The message is the human-readable string PipeWire
    /// returned (or a hand-written diagnostic when the failure is
    /// pre-handshake, e.g. `pw_context_connect` returning null). The
    /// connection flips back to disconnected before this signal fires.
    void error(const QString& message);
    /// Fired from the GUI thread when the registry reports a new audio
    /// node (Sink, Source, or Stream). The model classes filter by
    /// `node->mediaClass()`.
    void nodeAdded(PhosphorServicePipeWire::PwNode* node);
    /// Fired from the GUI thread BEFORE the PwNode is destroyed so
    /// observers (models) can detach. The pointer is still valid
    /// during this signal.
    void nodeRemoved(PhosphorServicePipeWire::PwNode* node);

private:
    class Private;
    std::unique_ptr<Private> d;
};

} // namespace PhosphorServicePipeWire
