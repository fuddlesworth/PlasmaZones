// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorServicePipeWire/phosphorservicepipewire_export.h>
// Full PwNode definition, not a forward declaration: the nodeAdded/
// nodeRemoved signals carry PwNode*, and Qt6 moc auto-registers signal
// parameter metatypes — QMetaType SFINAE-probes completeness, so a fwd decl
// here makes the probe fail and GCC's -Wsfinae-incomplete fire when
// moc_PwNode.cpp later defines the class in the same mocs_compilation TU.
#include <PhosphorServicePipeWire/PwNode.h>

#include <QList>
#include <QObject>
#include <QString>

#include <memory>

namespace PhosphorServicePipeWire {

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
///   daemon. Call `connectToDaemon()` to begin the handshake. `connected`
///   flips to true when `pw_core` reports the `done` event for the
///   initial sync, signalling that PipeWire acknowledged us.
/// - `disconnectFromDaemon()` tears down `pw_core` + `pw_context` and
///   pauses the loop. The thread itself stays alive; a subsequent
///   `connectToDaemon()` re-creates the context cheaply.
/// - Destruction quits the loop, joins the thread, and frees all
///   PipeWire state in the correct order (core → context → loop).
///
/// Error handling: PipeWire core errors (daemon restart, version
/// mismatch, transport drop) fire `error(message)` from the GUI thread
/// and flip `connected` back to false. The class does NOT auto-retry;
/// shells observe the error, decide on a backoff, and call
/// `connectToDaemon()` again. This keeps the policy in the shell rather
/// than baked into the library, mirroring how `phosphor-service-sni`'s
/// host treats the watcher-respawn case.
///
/// Slot naming: `connectToDaemon()` / `disconnectFromDaemon()` rather
/// than the bare `connect()` / `disconnect()` we'd otherwise pick — the
/// short names shadow QObject::connect / QObject::disconnect statics
/// (Qt machinery for wiring signals), and that shadow is a public API
/// trap any subclasser would hit. The verb-object form also mirrors the
/// sibling `reconnect()` on PipeWireHost.
class PHOSPHORSERVICEPIPEWIRE_EXPORT PipeWireConnection : public QObject
{
    Q_OBJECT
    Q_DISABLE_COPY_MOVE(PipeWireConnection)
    Q_PROPERTY(bool connected READ isConnected NOTIFY connectedChanged)
    /// True once the PipeWire daemon has answered the `pw_core` info
    /// event for the current connection (i.e. `pw_context_connect`
    /// succeeded AND the daemon shipped its core info). Distinct from
    /// `connected`: info-answered means the daemon is reachable, while
    /// `connected` additionally requires the post-info handshake sync
    /// to complete. QML uses `daemonAvailable` to drive the "PipeWire
    /// is installed but unreachable" diagnostic.
    /// Reset to false on three paths: (1) disconnect, (2) every
    /// pre-info failure (`pw_context_new` / `pw_context_connect`
    /// returning null, `pw_core_sync` failing), and (3) a post-info
    /// `pw_core` error reported via `onCoreError` (e.g. transient
    /// teardown, version mismatch), which clears `daemonAvailable`
    /// alongside `connected` to signal the daemon is no longer
    /// present even though it had answered info earlier.
    Q_PROPERTY(bool daemonAvailable READ isDaemonAvailable NOTIFY daemonAvailableChanged)
    /// Canonical PipeWire `node.name` of the WirePlumber default audio
    /// sink. Empty when no default metadata has been reported yet (no
    /// daemon, no WirePlumber, or pre-handshake). Tracks the
    /// `default.audio.sink` key on the WirePlumber `default` metadata.
    Q_PROPERTY(QString defaultSinkName READ defaultSinkName NOTIFY defaultSinkNameChanged)
    /// As above for the default audio source.
    Q_PROPERTY(QString defaultSourceName READ defaultSourceName NOTIFY defaultSourceNameChanged)

public:
    explicit PipeWireConnection(QObject* parent = nullptr);
    ~PipeWireConnection() override;

    // `is`-prefixed getters follow the Qt convention for bool Q_PROPERTY
    // READ accessors (matches QObject::isWidgetType, QWindow::isVisible,
    // etc.); the property name stays unprefixed (`connected`,
    // `daemonAvailable`) for the QML side.
    [[nodiscard]] bool isConnected() const;
    [[nodiscard]] bool isDaemonAvailable() const;
    [[nodiscard]] QString defaultSinkName() const;
    [[nodiscard]] QString defaultSourceName() const;
    /// Snapshot of every node the registry has reported so far, for
    /// C++ callers only. Live pointers — they're owned by this
    /// connection (QObject parent chain) and survive until the
    /// corresponding `nodeRemoved` signal fires. Returned by value
    /// (a freshly-built QList copy of the values from the internal
    /// `QHash<quint32, PwNode*>`), so the caller can iterate without
    /// locking; the underlying hash is mutated only on the GUI thread.
    /// Iteration order is unspecified — the underlying storage is a
    /// hash, not a sequence; do not rely on registry-insertion order.
    /// Call only from the GUI thread (the thread that owns this
    /// PipeWireConnection). The internal `guiNodes` hash is not
    /// synchronised; a cross-thread reader would race against the
    /// queued `nodeAdded` / `nodeRemoved` mutations that run here.
    ///
    /// QML consumers must NOT use this accessor: a by-value
    /// `QList<QObject*>` return is not bindable from QML. The
    /// supported QML view is `PwNodeModel` (and its `PwSinkModel`
    /// / `PwSourceModel` / `PwStreamModel` convenience subclasses),
    /// which observes `nodeAdded` / `nodeRemoved` and exposes the
    /// nodes as a live model.
    ///
    /// Reentrancy: do NOT iterate the returned list while pumping
    /// `QCoreApplication::processEvents()` on the GUI thread. The list
    /// is a value-copy of raw `PwNode*` pointers, but the nodes
    /// themselves are still owned by this connection — a queued
    /// `nodeRemoved` delivered during the pump can destroy a snapshot
    /// entry mid-iteration, leaving a dangling pointer in your loop.
    /// If you need to call back into Qt's event loop while iterating,
    /// copy the entries into `QPointer<PwNode>` first and check
    /// validity on each access.
    [[nodiscard]] QList<PwNode*> nodes() const;

public Q_SLOTS:
    /// Asynchronously establish a `pw_context` + `pw_core` and complete
    /// the initial sync. Safe to call from the GUI thread at any time;
    /// re-issuing while already connected is a no-op. Named with the
    /// `ToDaemon` suffix to avoid shadowing QObject::connect.
    void connectToDaemon();
    /// Asynchronously tear down `pw_core` + `pw_context`. The loop
    /// thread stays alive so a subsequent `connectToDaemon()` is cheap.
    /// Safe to call from the GUI thread at any time. Named with the
    /// `FromDaemon` suffix to avoid shadowing QObject::disconnect.
    void disconnectFromDaemon();
    /// Write the WirePlumber `default.configured.audio.sink` metadata
    /// key. PipeWire treats "configured" as the persistent default;
    /// the runtime `default.audio.sink` follows it. Pass the canonical
    /// node.name (visible via `PwNode::name` / the model's `name`
    /// role). No-op if no default metadata has been bound yet.
    void setDefaultSink(const QString& nodeName);
    /// As above for the audio source.
    void setDefaultSource(const QString& nodeName);
    /// Asynchronously write a per-channel volume array to the node
    /// with the given PipeWire global id. The write is dispatched onto
    /// the loop thread; consumers observe completion via the node's
    /// `propsChanged` signal once PipeWire echoes the updated Props
    /// pod. Called by `PwNode::setVolumes` / `setVolume`; advanced QML
    /// consumers may invoke directly when they have a node id but no
    /// PwNode* handle.
    ///
    /// Stale-id behavior: if `nodeId` refers to a node that has been
    /// removed (no longer in `nodes()`), the write is silently dropped
    /// on the loop thread without an error signal — the registry
    /// lookup misses and the slot returns. Callers that issue writes
    /// directly through this slot (rather than via `PwNode::setVolumes`
    /// / `setVolume`, which short-circuit on a removed parent) and
    /// need confirmation should check `nodes()` for the id before
    /// dispatching.
    void writeVolumes(quint32 nodeId, const QList<qreal>& volumes);
    /// Asynchronously write a mute state. Same dispatch + echo model
    /// as `writeVolumes`, including the same silent-drop behavior for
    /// stale ids — see `writeVolumes` for the rationale and the
    /// caller-side check recommendation.
    void writeMuted(quint32 nodeId, bool muted);

Q_SIGNALS:
    void connectedChanged();
    void daemonAvailableChanged();
    void defaultSinkNameChanged();
    void defaultSourceNameChanged();
    /// Fired from the GUI thread when PipeWire reports a core-level
    /// error. The message is the human-readable string PipeWire
    /// returned (or a hand-written diagnostic when the failure is
    /// pre-handshake, e.g. `pw_context_connect` returning null). The
    /// connection flips back to disconnected before this signal fires.
    void error(const QString& message);
    /// Fired from the GUI thread when the registry reports a new audio
    /// node (Sink, Source, or Stream). The model classes filter by
    /// `node->mediaClass()`. Node info / props are empty in this
    /// signal handler; subscribe to `PwNode::infoChanged` /
    /// `propsChanged` for populated values.
    /// PwNode* metatype is registered automatically in
    /// pipewireconnection.cpp's ensurePipeWireInit().
    ///
    /// FQN `PhosphorServicePipeWire::PwNode*` is intentional — moc's
    /// metatype registration matches the signature spelling, and the
    /// FQN dodges QML ambiguity in queued connections (the queued
    /// dispatcher resolves the parameter type by literal string match
    /// against the registered metatype name).
    void nodeAdded(PhosphorServicePipeWire::PwNode* node);
    /// Fired from the GUI thread BEFORE the PwNode is destroyed so
    /// observers (models) can detach. The pointer is still valid
    /// during this signal.
    /// PwNode* metatype is registered automatically in
    /// pipewireconnection.cpp's ensurePipeWireInit().
    /// Same FQN rationale as `nodeAdded` — keep the
    /// `PhosphorServicePipeWire::PwNode*` spelling for moc / queued
    /// dispatch.
    void nodeRemoved(PhosphorServicePipeWire::PwNode* node);

private:
    class Private;
    std::unique_ptr<Private> d;
};

} // namespace PhosphorServicePipeWire
