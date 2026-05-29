// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorServicePipeWire/PipeWireConnection.h>

#include <QHash>
#include <QLoggingCategory>
#include <QSemaphore>
#include <QString>
#include <QThread>

#include <pipewire/extensions/metadata.h>
#include <pipewire/pipewire.h>

#include <atomic>
#include <memory>
#include <unordered_map>

Q_DECLARE_LOGGING_CATEGORY(lcPipeWire)

namespace PhosphorServicePipeWire {

class PwNode;

namespace detail {

/// `pw_init` is process-global and not idempotent. Guard with
/// `call_once` so multiple `PipeWireConnection` instances (rare but
/// possible in test fixtures) don't double-initialise.
void ensurePipeWireInit();

/// True when the node's `media.class` property is one of the audio
/// classes the mixer surface cares about. The authoritative list lives
/// in the implementation; do not duplicate it here.
bool isAudioNodeClass(const QString& mediaClass);

/// Convert a SPA dict to a QHash of QString → QString.
QHash<QString, QString> propsFromDict(const struct spa_dict* dict);

} // namespace detail

class PipeWireConnection::Private
{
public:
    explicit Private(PipeWireConnection* qPtr)
        : q(qPtr)
    {
    }

    PipeWireConnection* q = nullptr;

    /// Worker thread that runs `pw_main_loop_run`. Qt's QThread default
    /// `run()` starts a Qt event loop via `exec()`. We can't use it:
    /// `pw_main_loop_run` blocks for the lifetime of the connection
    /// and Qt's event loop would never get scheduling time. Subclass
    /// `QThread` and override `run()` to call `pw_main_loop_run`
    /// directly; the Qt event loop on this thread is intentionally
    /// never started.
    class LoopThread : public QThread
    {
    public:
        explicit LoopThread(Private* owner)
            : QThread(nullptr)
            , owner(owner)
        {
            setObjectName(QStringLiteral("PipeWireLoop"));
        }

    protected:
        void run() override;

    private:
        Private* owner = nullptr;
    };

    LoopThread thread{this};

    /// Released by the worker thread once `pw_main_loop_new` has
    /// either succeeded or failed (so `loop` has its final value).
    /// The constructor acquires this before returning so the
    /// destructor never observes the worker mid-startup.
    QSemaphore startupReady{0};

    /// `pw_main_loop*` is touched from both threads: the loop thread
    /// owns its lifetime (allocates in LoopThread::run, clears on exit),
    /// while the GUI thread reads it on every connect/disconnect/quit
    /// call. A raw pointer would be a torn-read race on architectures
    /// where pointer loads aren't atomic. Use a std::atomic with
    /// acquire/release ordering so the GUI thread either sees the fully
    /// constructed loop or null — never a half-published pointer.
    std::atomic<pw_main_loop*> loop{nullptr};
    pw_context* context = nullptr;
    pw_core* core = nullptr;
    pw_registry* registry = nullptr;
    /// WirePlumber's `default` metadata proxy. Bound lazily when the
    /// registry surfaces a Metadata global whose `metadata.name`
    /// property equals `"default"`. Null when no session manager is
    /// present (rare: only a bare PipeWire daemon without
    /// WirePlumber) or pre-handshake.
    pw_proxy* defaultMetadata = nullptr;
    /// Registry id captured at bind time for `defaultMetadata`. We
    /// cache it ourselves because `pw_proxy_get_bound_id` returns
    /// `SPA_ID_INVALID` until the daemon answers with a `bound_id`
    /// event; if a `global_remove` fires before that ack, the registry
    /// id is our only handle for identifying the removed metadata
    /// proxy. Cleared (back to `SPA_ID_INVALID`) whenever
    /// `defaultMetadata` is destroyed via `pw_proxy_destroy`; never set
    /// unless the corresponding `defaultMetadata` is bound. The
    /// invariant is therefore: `defaultMetadataId != SPA_ID_INVALID`
    /// implies `defaultMetadata != nullptr`. A bind-failure path that
    /// leaves `defaultMetadata` null never assigns the id, so the
    /// invariant holds vacuously without an explicit reset there.
    quint32 defaultMetadataId = SPA_ID_INVALID;
    spa_hook coreListener{};
    spa_hook registryListener{};
    spa_hook defaultMetadataListener{};
    /// Sequence number returned by `pw_core_sync`, matched against the
    /// `seq` field of the corresponding `done` event to decide whether
    /// the handshake completed. Initialised to `kNoPendingSync` (a
    /// sentinel PipeWire never emits, since pw_core seq numbers are
    /// non-negative) so an unrelated seq=0 done event arriving before
    /// our sync request landed cannot be misread as our handshake.
    /// Reset back to the sentinel on disconnect and on sync-submission
    /// failure.
    ///
    /// Loop-thread only — written from doConnect and the core
    /// callbacks (onCoreDone, onCoreError) and read from onCoreDone's
    /// equality check, all of which execute on the PipeWire loop
    /// thread. Same lifetime model as the wedged flag below; no
    /// cross-thread synchronisation needed.
private:
    /// Implementation constant for the pendingSyncSeq sentinel.
    /// Scoped private explicitly so it is consumed only via the
    /// member callbacks above, never by external code reaching into
    /// the pimpl through a header it shouldn't have seen.
    static constexpr int kNoPendingSync = -1;

public:
    int pendingSyncSeq = kNoPendingSync;

    /// Loop-thread flag: set by onCoreError when a core-level error
    /// fires while `core` is still alive, cleared by doConnect (after
    /// the recovery teardown) and doDisconnect (after a full teardown).
    /// doConnect gates its wedge-recovery teardown on this flag rather
    /// than inferring it from `!connected.load()` — an in-flight
    /// handshake also has `connected == false` but is not wedged, and
    /// must not be torn down by a re-entrant connectToDaemon() call.
    bool wedged = false;

    /// Per-node loop-thread state. We hold the pw_proxy for the node
    /// (for SPA_PARAM_Props enumeration) and the spa_hook for its
    /// listener wire. Keyed by PipeWire global id. Only touched on
    /// the loop thread. `owner` is the back-pointer the node-listener
    /// callbacks use to post results onto the GUI thread; each
    /// LoopNode is passed as the listener `data` so the callbacks can
    /// recover both the node id and the Private* in one indirection.
    struct LoopNode
    {
        Private* owner = nullptr;
        quint32 id = 0;
        QString mediaClass;
        pw_proxy* proxy = nullptr;
        spa_hook nodeListener{};
        // The spa_hook is intrusively linked through the entry's
        // storage; a silent copy would clone the link node and split
        // it from its real owner, leaving dangling list pointers in
        // the listener wire. Forbid copies at compile time.
        Q_DISABLE_COPY(LoopNode)
        LoopNode() = default;
    };
    /// std::unordered_map (not QHash) because QHash requires its value
    /// type to be copyable for rehashing and std::unique_ptr is move-
    /// only. The owning-pointer pattern is essential: each LoopNode's
    /// address is baked into the spa_hook listener wires, so the
    /// pointer must stay stable for the listener's lifetime.
    std::unordered_map<quint32, std::unique_ptr<LoopNode>> loopNodes;

    /// GUI-thread state. Authoritative map of typed PwNode QObjects;
    /// keyed by PipeWire global id. The QObject parent is `q` so the
    /// nodes are torn down automatically when the connection dies.
    /// Touched only on the GUI thread.
    QHash<quint32, PwNode*> guiNodes;

    // Snapshot of state for GUI-thread getters. The atomics carry the
    // cross-thread truth: loop-thread callbacks (onCoreInfo,
    // onCoreDone, onCoreError, doConnect failure paths, and
    // doDisconnect) flip them synchronously so isConnected() /
    // isDaemonAvailable() report the truth the moment the next caller
    // looks. The GUI-thread `lastEmitted*` shadows track what observers
    // were last told via the NOTIFY signal; setConnected /
    // setDaemonAvailable consult those (not the atomic) to decide
    // whether a real transition needs to be announced. This separation
    // is what makes the "synchronous flip + queued setter" pattern
    // emit exactly one connectedChanged per observable transition even
    // when the atomic has already been pre-flipped on the loop thread.
    std::atomic<bool> connected{false};
    std::atomic<bool> daemonAvailable{false};
    bool lastEmittedConnected = false;
    bool lastEmittedDaemonAvailable = false;

    // String state touched only on the GUI thread (read via the
    // accessors, written by the GUI-thread setter slots posted from
    // loop callbacks). No mutex needed.
    QString defaultSinkName;
    QString defaultSourceName;

    // Core listener callbacks. Each runs on the loop thread; bounce
    // state back to the GUI thread via QMetaObject::invokeMethod with
    // QueuedConnection (which targets the GUI thread, NOT the loop
    // thread, because q lives on the GUI thread).
    static void onCoreInfo(void* data, const struct pw_core_info* info);
    static void onCoreDone(void* data, uint32_t id, int seq);
    static void onCoreError(void* data, uint32_t id, int seq, int res, const char* message);
    static const pw_core_events kCoreEvents;

    // Registry callbacks.
    static void onRegistryGlobal(void* data, uint32_t id, uint32_t permissions, const char* type, uint32_t version,
                                 const struct spa_dict* props);
    static void onRegistryGlobalRemove(void* data, uint32_t id);
    static const pw_registry_events kRegistryEvents;

    // onRegistryGlobal dispatch helpers. Split out so the C callback
    // stays a thin type-routing switch instead of an 80-line blob that
    // mixes metadata binding with audio-node binding-and-tracking.
    void bindDefaultMetadata(uint32_t id, const char* type, const QHash<QString, QString>& props);
    void bindAudioNode(uint32_t id, const char* type, const QString& mediaClass, const QHash<QString, QString>& props);

    // Per-node listener callbacks (loop thread).
    static void onNodeInfo(void* data, const struct pw_node_info* info);
    static void onNodeParam(void* data, int seq, uint32_t id, uint32_t index, uint32_t next,
                            const struct spa_pod* param);
    static const pw_node_events kNodeEvents;

    // WirePlumber default-metadata listener (loop thread).
    static int onDefaultMetadataProperty(void* data, uint32_t subject, const char* key, const char* type,
                                         const char* value);
    static const pw_metadata_events kDefaultMetadataEvents;

    /// Called on the loop thread. Creates `pw_context` + `pw_core` and
    /// kicks off the handshake. Idempotent: re-entry while already
    /// connected is a no-op.
    void doConnect();
    /// Called on the loop thread. Tears down `pw_core` + `pw_context`.
    /// Idempotent.
    void doDisconnect();

    // Cross-thread dispatch shims. pw_loop_invoke is the documented
    // mechanism for posting work onto the PipeWire loop thread from
    // outside; it queues `fn` to run on the loop thread the next time
    // the loop processes its event queue. The `data` parameter is
    // ignored here (we pass user_data = this and ignore the typed
    // payload).
    static int dispatchConnect(struct spa_loop* loop, bool async, uint32_t seq, const void* data, size_t size,
                               void* user_data);
    static int dispatchDisconnect(struct spa_loop* loop, bool async, uint32_t seq, const void* data, size_t size,
                                  void* user_data);

    /// Loop-thread request carrying a volume + mute write to a single
    /// node. The non-POD members (QList) are why we heap-allocate the
    /// struct and pass the pointer through pw_loop_invoke's user_data
    /// rather than the data payload, which is memcpy'd internally.
    struct ParamWriteRequest
    {
        Private* owner = nullptr;
        quint32 nodeId = 0;
        bool writeVolumes = false;
        bool writeMuted = false;
        QList<qreal> volumes;
        bool muted = false;
    };
    static int dispatchParamWrite(struct spa_loop* loop, bool async, uint32_t seq, const void* data, size_t size,
                                  void* user_data);
    void doParamWrite(const ParamWriteRequest& req);

    /// Loop-thread default-metadata write. Distinct from the param
    /// write path because the WirePlumber metadata interface uses
    /// pw_metadata_set_property rather than pw_node_set_param.
    struct DefaultWriteRequest
    {
        Private* owner = nullptr;
        QString key;
        QString nodeName;
    };
    static int dispatchDefaultWrite(struct spa_loop* loop, bool async, uint32_t seq, const void* data, size_t size,
                                    void* user_data);
    void doDefaultWrite(const DefaultWriteRequest& req);

    /// Submit a heap-allocated loop request via pw_loop_invoke.
    /// Centralises the running/loop guard, the rc < 0 check, the
    /// ownership-release dance, and the warning log line so each
    /// public write entry point becomes a 2-line request builder.
    /// Returns true if the request was queued (ownership transferred
    /// to the dispatcher); false on guard failure or dispatch error
    /// (req is then destroyed by the unique_ptr going out of scope).
    template<typename Req>
    bool submitLoopRequest(std::unique_ptr<Req> req,
                           int (*dispatcher)(struct spa_loop*, bool, uint32_t, const void*, size_t, void*),
                           const char* label);

    // GUI-thread setters. Invoked via queued cross-thread signals;
    // they only shadow-dedup and emit NOTIFY. The atomics are
    // loop-thread truth, written synchronously by the callers
    // (onCoreInfo, onCoreDone, onCoreError, doConnect failure paths,
    // doDisconnect) BEFORE queueing these setters — these setters
    // never touch the atomic. The string setters carry no atomic and
    // simply value-equal-skip + emit.
    void setConnected(bool value);
    void setDaemonAvailable(bool value);
    void setDefaultSinkName(QString name);
    void setDefaultSourceName(QString name);

    /// GUI-thread helper: reset every cached snapshot that the public
    /// API exposes (connected, daemonAvailable, default sink/source
    /// names). Used by doConnect's failure paths and doDisconnect so a
    /// failed reconnect can't leave stale defaults from a previous
    /// session ghosting the UI. Each setter dedupes via shadow-equal-
    /// skip (booleans) or value-equality compare (strings), so
    /// observers see at most one NOTIFY per genuinely-changed
    /// property.
    void resetGuiSnapshot();

    // GUI-thread handlers, posted from the loop callbacks via
    // QueuedConnection. These mutate `guiNodes` and emit nodeAdded /
    // nodeRemoved on q.
    void guiNodeAdded(quint32 id, QString mediaClass, QHash<QString, QString> props);
    void guiNodeInfo(quint32 id, QHash<QString, QString> props);
    void guiNodeRemoved(quint32 id);
    void guiNodesReset();
};

} // namespace PhosphorServicePipeWire
