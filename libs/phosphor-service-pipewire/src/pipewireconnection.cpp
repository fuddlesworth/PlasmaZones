// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorServicePipeWire/PipeWireConnection.h>

#include <PhosphorServicePipeWire/PwNode.h>

#include <QHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLoggingCategory>
#include <QSemaphore>
#include <QThread>

#include <pipewire/extensions/metadata.h>
#include <pipewire/pipewire.h>
#include <spa/param/audio/raw.h>
#include <spa/param/props.h>
#include <spa/pod/builder.h>
#include <spa/pod/iter.h>
#include <spa/pod/parser.h>
#include <spa/utils/result.h>

#include <atomic>
#include <mutex>
#include <unordered_map>

Q_LOGGING_CATEGORY(lcPipeWire, "phosphor.service.pipewire.connection")

namespace PhosphorServicePipeWire {

namespace {

/// `pw_init` / `pw_deinit` are process-global and not idempotent. Guard
/// them with a `call_once` so multiple `PipeWireConnection` instances
/// (rare but possible in test fixtures) don't double-initialise.
std::once_flag g_pwInitOnce;

void ensurePipeWireInit()
{
    std::call_once(g_pwInitOnce, [] {
        pw_init(nullptr, nullptr);
    });
}

bool isAudioNodeClass(const QString& mediaClass)
{
    return mediaClass == QLatin1String("Audio/Sink") || mediaClass == QLatin1String("Audio/Source")
        || mediaClass == QLatin1String("Stream/Output/Audio") || mediaClass == QLatin1String("Stream/Input/Audio");
}

QHash<QString, QString> propsFromDict(const struct spa_dict* dict)
{
    QHash<QString, QString> out;
    if (!dict)
        return out;
    const struct spa_dict_item* item = nullptr;
    spa_dict_for_each(item, dict)
    {
        if (!item || !item->key)
            continue;
        out.insert(QString::fromUtf8(item->key), item->value ? QString::fromUtf8(item->value) : QString());
    }
    return out;
}

} // namespace

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
        void run() override
        {
            owner->loop = pw_main_loop_new(nullptr);
            // Signal startup completion to the constructor whether or
            // not the loop was created. The constructor blocks on this
            // semaphore so the destructor never races against a worker
            // that hasn't yet reached pw_main_loop_new.
            owner->startupReady.release();
            if (!owner->loop) {
                qCWarning(lcPipeWire) << "pw_main_loop_new failed; PipeWire integration disabled";
                return;
            }
            // Blocks until pw_main_loop_quit fires.
            pw_main_loop_run(owner->loop);
            // Tear down any remaining loop-owned state on the loop
            // thread, in the correct order, before exit. doDisconnect
            // is idempotent.
            owner->doDisconnect();
            pw_main_loop_destroy(owner->loop);
            owner->loop = nullptr;
        }

    private:
        Private* owner = nullptr;
    };

    LoopThread thread{this};

    /// Released by the worker thread once `pw_main_loop_new` has
    /// either succeeded or failed (so `loop` has its final value).
    /// The constructor acquires this before returning so the
    /// destructor never observes the worker mid-startup.
    QSemaphore startupReady{0};

    // Loop-side state. ONLY accessed from the loop thread.
    pw_main_loop* loop = nullptr;
    pw_context* context = nullptr;
    pw_core* core = nullptr;
    pw_registry* registry = nullptr;
    /// WirePlumber's `default` metadata proxy. Bound lazily when the
    /// registry surfaces a Metadata global whose `metadata.name`
    /// property equals `"default"`. Null when no session manager is
    /// present (rare — only a bare PipeWire daemon without
    /// WirePlumber) or pre-handshake.
    pw_proxy* defaultMetadata = nullptr;
    spa_hook coreListener{};
    spa_hook registryListener{};
    spa_hook defaultMetadataListener{};
    int pendingSyncSeq = 0;

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

    // Snapshot of state for GUI-thread getters. Updated by the GUI
    // thread itself via queued cross-thread signals; the atomics let
    // a property-system getter return the cached value without a
    // round-trip to the worker thread.
    std::atomic<bool> connected{false};
    std::atomic<bool> daemonAvailable{false};

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
        bool writeMute = false;
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

    // GUI-thread setters. Always invoked via queued cross-thread
    // signals so the NOTIFY emits and atomic writes both happen on
    // the GUI thread.
    void setConnected(bool value);
    void setDaemonAvailable(bool value);
    void setDefaultSinkName(QString name);
    void setDefaultSourceName(QString name);

    // GUI-thread handlers, posted from the loop callbacks via
    // QueuedConnection. These mutate `guiNodes` and emit nodeAdded /
    // nodeRemoved on q.
    void guiNodeAdded(quint32 id, QString mediaClass, QHash<QString, QString> props);
    void guiNodeInfo(quint32 id, QHash<QString, QString> props);
    void guiNodeProps(quint32 id, int channelCount, QList<qreal> volumes, bool muted);
    void guiNodeRemoved(quint32 id);
    void guiNodesReset();
};

const pw_core_events PipeWireConnection::Private::kCoreEvents = {
    .version = PW_VERSION_CORE_EVENTS,
    .info = &PipeWireConnection::Private::onCoreInfo,
    .done = &PipeWireConnection::Private::onCoreDone,
    .ping = nullptr,
    .error = &PipeWireConnection::Private::onCoreError,
    .remove_id = nullptr,
    .bound_id = nullptr,
    .add_mem = nullptr,
    .remove_mem = nullptr,
    .bound_props = nullptr,
};

const pw_registry_events PipeWireConnection::Private::kRegistryEvents = {
    .version = PW_VERSION_REGISTRY_EVENTS,
    .global = &PipeWireConnection::Private::onRegistryGlobal,
    .global_remove = &PipeWireConnection::Private::onRegistryGlobalRemove,
};

const pw_node_events PipeWireConnection::Private::kNodeEvents = {
    .version = PW_VERSION_NODE_EVENTS,
    .info = &PipeWireConnection::Private::onNodeInfo,
    .param = &PipeWireConnection::Private::onNodeParam,
};

const pw_metadata_events PipeWireConnection::Private::kDefaultMetadataEvents = {
    .version = PW_VERSION_METADATA_EVENTS,
    .property = &PipeWireConnection::Private::onDefaultMetadataProperty,
};

void PipeWireConnection::Private::onCoreInfo(void* data, const struct pw_core_info* info)
{
    auto* d = static_cast<Private*>(data);
    qCDebug(lcPipeWire) << "pw_core info: name" << (info && info->name ? info->name : "(null)") << "version"
                        << (info ? info->version : "(null)");
    // Daemon answered; flip daemonAvailable BEFORE the done event so a
    // fast-failing test can distinguish "no daemon" from "daemon
    // present, handshake mid-flight".
    QMetaObject::invokeMethod(
        d->q,
        [d]() {
            d->setDaemonAvailable(true);
        },
        Qt::QueuedConnection);
}

void PipeWireConnection::Private::onCoreDone(void* data, uint32_t id, int seq)
{
    auto* d = static_cast<Private*>(data);
    if (id != PW_ID_CORE || seq != d->pendingSyncSeq)
        return;
    qCDebug(lcPipeWire) << "pw_core done seq" << seq << "— handshake complete";
    QMetaObject::invokeMethod(
        d->q,
        [d]() {
            d->setConnected(true);
        },
        Qt::QueuedConnection);
}

void PipeWireConnection::Private::onCoreError(void* data, uint32_t id, int seq, int res, const char* message)
{
    Q_UNUSED(id);
    Q_UNUSED(seq);
    auto* d = static_cast<Private*>(data);
    const QString msg = QStringLiteral("PipeWire core error %1: %2")
                            .arg(res)
                            .arg(message ? QString::fromUtf8(message) : QStringLiteral("(no message)"));
    qCWarning(lcPipeWire) << msg;
    QMetaObject::invokeMethod(
        d->q,
        [d, msg]() {
            d->setConnected(false);
            Q_EMIT d->q->error(msg);
        },
        Qt::QueuedConnection);
}

void PipeWireConnection::Private::onRegistryGlobal(void* data, uint32_t id, uint32_t permissions, const char* type,
                                                   uint32_t version, const struct spa_dict* props)
{
    Q_UNUSED(permissions);
    Q_UNUSED(version);
    auto* d = static_cast<Private*>(data);
    if (!type)
        return;
    const std::string_view typeView(type);
    // Two interfaces are relevant: Node (audio sinks / sources /
    // streams) and Metadata (WirePlumber's `default` for the runtime
    // default audio sink + source).
    if (typeView == PW_TYPE_INTERFACE_Metadata) {
        const auto metaProps = propsFromDict(props);
        if (metaProps.value(QStringLiteral("metadata.name")) != QLatin1String("default"))
            return;
        if (d->defaultMetadata) {
            qCDebug(lcPipeWire) << "duplicate default-metadata global; ignoring id" << id;
            return;
        }
        d->defaultMetadata = static_cast<pw_proxy*>(pw_registry_bind(d->registry, id, type, PW_VERSION_METADATA, 0));
        if (!d->defaultMetadata) {
            qCWarning(lcPipeWire) << "pw_registry_bind failed for default metadata global" << id;
            return;
        }
        pw_metadata_add_listener(reinterpret_cast<pw_metadata*>(d->defaultMetadata), &d->defaultMetadataListener,
                                 &kDefaultMetadataEvents, d);
        qCDebug(lcPipeWire) << "bound default metadata id" << id;
        return;
    }
    if (typeView != PW_TYPE_INTERFACE_Node)
        return;
    const auto p = propsFromDict(props);
    const QString mediaClass = p.value(QStringLiteral("media.class"));
    if (!isAudioNodeClass(mediaClass)) {
        qCDebug(lcPipeWire) << "skipping non-audio node" << id << "mediaClass" << mediaClass;
        return;
    }
    qCDebug(lcPipeWire) << "audio node added: id" << id << "mediaClass" << mediaClass << "name"
                        << p.value(QStringLiteral("node.name"));

    // Bind the proxy on the loop thread so we can subscribe to its
    // info + param events. Track it in loopNodes for cleanup on
    // global_remove.
    auto* proxy = static_cast<pw_proxy*>(pw_registry_bind(d->registry, id, type, PW_VERSION_NODE, 0));
    if (!proxy) {
        qCWarning(lcPipeWire) << "pw_registry_bind failed for node" << id;
        return;
    }
    auto entry = std::make_unique<LoopNode>();
    entry->owner = d;
    entry->id = id;
    entry->mediaClass = mediaClass;
    entry->proxy = proxy;
    LoopNode* entryPtr = entry.get();
    pw_node_add_listener(reinterpret_cast<pw_node*>(proxy), &entryPtr->nodeListener, &kNodeEvents, entryPtr);
    // Pre-arm SPA_PARAM_Props subscription so the next param event
    // delivers the current volume + mute. The Props pod streams in
    // via onNodeParam.
    pw_node_enum_params(reinterpret_cast<pw_node*>(proxy), 0, SPA_PARAM_Props, 0, UINT32_MAX, nullptr);
    d->loopNodes.emplace(id, std::move(entry));

    QMetaObject::invokeMethod(
        d->q,
        [d, id, mediaClass, p]() {
            d->guiNodeAdded(id, mediaClass, p);
        },
        Qt::QueuedConnection);
}

void PipeWireConnection::Private::onRegistryGlobalRemove(void* data, uint32_t id)
{
    auto* d = static_cast<Private*>(data);
    // Same id space covers nodes AND the metadata global, so check
    // the metadata first.
    if (d->defaultMetadata) {
        const uint32_t metaId = pw_proxy_get_bound_id(d->defaultMetadata);
        if (metaId == id) {
            spa_hook_remove(&d->defaultMetadataListener);
            pw_proxy_destroy(d->defaultMetadata);
            d->defaultMetadata = nullptr;
            QMetaObject::invokeMethod(
                d->q,
                [d]() {
                    d->setDefaultSinkName(QString());
                    d->setDefaultSourceName(QString());
                },
                Qt::QueuedConnection);
            return;
        }
    }
    auto it = d->loopNodes.find(id);
    if (it == d->loopNodes.end())
        return;
    auto entry = std::move(it->second);
    d->loopNodes.erase(it);
    if (entry->proxy) {
        spa_hook_remove(&entry->nodeListener);
        pw_proxy_destroy(entry->proxy);
    }
    QMetaObject::invokeMethod(
        d->q,
        [d, id]() {
            d->guiNodeRemoved(id);
        },
        Qt::QueuedConnection);
}

int PipeWireConnection::Private::onDefaultMetadataProperty(void* data, uint32_t subject, const char* key,
                                                           const char* type, const char* value)
{
    Q_UNUSED(subject);
    Q_UNUSED(type);
    if (!key)
        return 0;
    auto* d = static_cast<Private*>(data);
    const QString keyStr = QString::fromUtf8(key);
    // We care about the runtime default keys. The "configured"
    // variants are persistent storage; we read the runtime ones so the
    // value tracks the currently-effective default (which may differ
    // from the persistent setting if a higher-priority sink appears).
    const bool isSink = keyStr == QLatin1String("default.audio.sink");
    const bool isSource = keyStr == QLatin1String("default.audio.source");
    if (!isSink && !isSource)
        return 0;
    QString nodeName;
    if (value) {
        // WirePlumber stores the value as a Spa:String:JSON object
        // shaped `{"name": "<node.name>"}`. Parse defensively — a
        // future schema bump could add fields and we should still pick
        // up the name.
        const QJsonDocument doc = QJsonDocument::fromJson(QByteArray(value));
        if (doc.isObject())
            nodeName = doc.object().value(QStringLiteral("name")).toString();
    }
    QMetaObject::invokeMethod(
        d->q,
        [d, isSink, nodeName]() {
            if (isSink)
                d->setDefaultSinkName(nodeName);
            else
                d->setDefaultSourceName(nodeName);
        },
        Qt::QueuedConnection);
    return 0;
}

void PipeWireConnection::Private::onNodeInfo(void* data, const struct pw_node_info* info)
{
    if (!info)
        return;
    auto* entry = static_cast<LoopNode*>(data);
    if (!entry || !entry->owner)
        return;
    auto* d = entry->owner;
    const quint32 id = entry->id;
    const auto props = propsFromDict(info->props);
    QMetaObject::invokeMethod(
        d->q,
        [d, id, props]() {
            d->guiNodeInfo(id, props);
        },
        Qt::QueuedConnection);
}

void PipeWireConnection::Private::onNodeParam(void* data, int seq, uint32_t paramId, uint32_t index, uint32_t next,
                                              const struct spa_pod* param)
{
    Q_UNUSED(seq);
    Q_UNUSED(index);
    Q_UNUSED(next);
    if (!param || paramId != SPA_PARAM_Props)
        return;
    auto* entry = static_cast<LoopNode*>(data);
    if (!entry || !entry->owner)
        return;
    auto* d = entry->owner;
    const quint32 nodeId = entry->id;

    // Parse the Props pod for SPA_PROP_channelVolumes + SPA_PROP_mute.
    // PipeWire builds the pod incrementally over the node's lifetime;
    // a given pod may carry one, both, or neither of the fields we
    // care about. Track which we observed so we can keep the previous
    // value for missing fields rather than clobbering with defaults.
    int channelCount = 0;
    QList<qreal> volumes;
    bool muted = false;
    bool haveVolumes = false;
    bool haveMute = false;
    struct spa_pod_prop* prop = nullptr;
    SPA_POD_OBJECT_FOREACH(reinterpret_cast<const struct spa_pod_object*>(param), prop)
    {
        if (prop->key == SPA_PROP_channelVolumes) {
            float values[SPA_AUDIO_MAX_CHANNELS] = {};
            const uint32_t n = spa_pod_copy_array(&prop->value, SPA_TYPE_Float, values, SPA_AUDIO_MAX_CHANNELS);
            channelCount = static_cast<int>(n);
            volumes.reserve(channelCount);
            for (uint32_t i = 0; i < n; ++i) {
                volumes.append(static_cast<qreal>(values[i]));
            }
            haveVolumes = true;
        } else if (prop->key == SPA_PROP_mute) {
            bool m = false;
            if (spa_pod_get_bool(&prop->value, &m) == 0) {
                muted = m;
                haveMute = true;
            }
        }
    }

    if (!haveVolumes && !haveMute) {
        // Pod carried neither field — nothing observable changed for
        // our purposes. Skip the GUI hop entirely.
        return;
    }

    QMetaObject::invokeMethod(
        d->q,
        [d, nodeId, channelCount, volumes, muted, haveVolumes, haveMute]() {
            auto it = d->guiNodes.find(nodeId);
            if (it == d->guiNodes.end())
                return;
            // Preserve the previous value for any field the pod didn't
            // carry. applyProps emits propsChanged only on actual
            // observable movement.
            const int finalCount = haveVolumes ? channelCount : it.value()->channelCount();
            const QList<qreal> finalVolumes = haveVolumes ? volumes : it.value()->volumes();
            const bool finalMuted = haveMute ? muted : it.value()->muted();
            it.value()->applyProps(finalCount, finalVolumes, finalMuted);
        },
        Qt::QueuedConnection);
}

void PipeWireConnection::Private::doConnect()
{
    if (core)
        return; // already connected (or mid-handshake)
    if (!loop) {
        qCWarning(lcPipeWire) << "doConnect called before loop creation";
        return;
    }
    context = pw_context_new(pw_main_loop_get_loop(loop), nullptr, 0);
    if (!context) {
        QMetaObject::invokeMethod(
            q,
            [this]() {
                setDaemonAvailable(false);
                Q_EMIT q->error(QStringLiteral("pw_context_new returned null"));
            },
            Qt::QueuedConnection);
        return;
    }
    core = pw_context_connect(context, nullptr, 0);
    if (!core) {
        pw_context_destroy(context);
        context = nullptr;
        QMetaObject::invokeMethod(
            q,
            [this]() {
                setDaemonAvailable(false);
                Q_EMIT q->error(QStringLiteral("pw_context_connect returned null (no running daemon?)"));
            },
            Qt::QueuedConnection);
        return;
    }
    pw_core_add_listener(core, &coreListener, &kCoreEvents, this);

    registry = pw_core_get_registry(core, PW_VERSION_REGISTRY, 0);
    if (registry) {
        pw_registry_add_listener(registry, &registryListener, &kRegistryEvents, this);
    } else {
        qCWarning(lcPipeWire) << "pw_core_get_registry returned null; registry walking disabled";
    }

    // Sync request: the matching `done` event signals "PipeWire has
    // processed every prior request". We use it as our handshake
    // completion marker.
    pendingSyncSeq = pw_core_sync(core, PW_ID_CORE, 0);
}

void PipeWireConnection::Private::doDisconnect()
{
    // Tear down per-node state before the registry / core so the
    // listener removals run while the proxy + core still exist.
    for (auto& kv : loopNodes) {
        auto& entry = kv.second;
        if (entry && entry->proxy) {
            spa_hook_remove(&entry->nodeListener);
            pw_proxy_destroy(entry->proxy);
            entry->proxy = nullptr;
        }
    }
    loopNodes.clear();

    if (defaultMetadata) {
        spa_hook_remove(&defaultMetadataListener);
        pw_proxy_destroy(defaultMetadata);
        defaultMetadata = nullptr;
    }

    if (registry) {
        spa_hook_remove(&registryListener);
        pw_proxy_destroy(reinterpret_cast<pw_proxy*>(registry));
        registry = nullptr;
    }
    if (core) {
        spa_hook_remove(&coreListener);
        pw_core_disconnect(core);
        core = nullptr;
    }
    if (context) {
        pw_context_destroy(context);
        context = nullptr;
    }
    pendingSyncSeq = 0;
    QMetaObject::invokeMethod(
        q,
        [this]() {
            setConnected(false);
            setDaemonAvailable(false);
            setDefaultSinkName(QString());
            setDefaultSourceName(QString());
            guiNodesReset();
        },
        Qt::QueuedConnection);
}

int PipeWireConnection::Private::dispatchConnect(struct spa_loop* loop, bool async, uint32_t seq, const void* data,
                                                 size_t size, void* user_data)
{
    Q_UNUSED(loop);
    Q_UNUSED(async);
    Q_UNUSED(seq);
    Q_UNUSED(data);
    Q_UNUSED(size);
    static_cast<Private*>(user_data)->doConnect();
    return 0;
}

int PipeWireConnection::Private::dispatchParamWrite(struct spa_loop* loop, bool async, uint32_t seq, const void* data,
                                                    size_t size, void* user_data)
{
    Q_UNUSED(loop);
    Q_UNUSED(async);
    Q_UNUSED(seq);
    Q_UNUSED(data);
    Q_UNUSED(size);
    // The request was heap-allocated by writeVolumes/writeMuted; take
    // ownership via unique_ptr so it's released after the handler
    // returns (even if doParamWrite throws — unlikely in C-bound
    // code, but the guarantee is cheap).
    std::unique_ptr<ParamWriteRequest> req(static_cast<ParamWriteRequest*>(user_data));
    if (req && req->owner)
        req->owner->doParamWrite(*req);
    return 0;
}

int PipeWireConnection::Private::dispatchDefaultWrite(struct spa_loop* loop, bool async, uint32_t seq, const void* data,
                                                      size_t size, void* user_data)
{
    Q_UNUSED(loop);
    Q_UNUSED(async);
    Q_UNUSED(seq);
    Q_UNUSED(data);
    Q_UNUSED(size);
    std::unique_ptr<DefaultWriteRequest> req(static_cast<DefaultWriteRequest*>(user_data));
    if (req && req->owner)
        req->owner->doDefaultWrite(*req);
    return 0;
}

void PipeWireConnection::Private::doDefaultWrite(const DefaultWriteRequest& req)
{
    if (!defaultMetadata) {
        qCDebug(lcPipeWire) << "default-write before metadata bind; dropping" << req.key;
        return;
    }
    // WirePlumber stores defaults as Spa:String:JSON values shaped
    // `{"name": "<node.name>"}`. Build the JSON via QJsonDocument so
    // the encoding (quoting, escaping) is consistent with the
    // metadata reader path.
    QJsonObject obj;
    obj.insert(QStringLiteral("name"), req.nodeName);
    const QByteArray payload = QJsonDocument(obj).toJson(QJsonDocument::Compact);
    pw_metadata_set_property(reinterpret_cast<pw_metadata*>(defaultMetadata), 0, req.key.toUtf8().constData(),
                             "Spa:String:JSON", payload.constData());
}

void PipeWireConnection::Private::doParamWrite(const ParamWriteRequest& req)
{
    auto it = loopNodes.find(req.nodeId);
    if (it == loopNodes.end() || !it->second || !it->second->proxy) {
        qCDebug(lcPipeWire) << "param write for unknown / detached node" << req.nodeId;
        return;
    }
    auto* node = reinterpret_cast<pw_node*>(it->second->proxy);

    // Build a Props pod containing only the field(s) we're updating.
    // The buffer size budget: 1KiB is enough for a single Props
    // object with up to ~64 channel-volume floats plus the SPA
    // bookkeeping overhead — SPA_AUDIO_MAX_CHANNELS is 64 today, so
    // the worst case is well under 512 bytes.
    uint8_t buffer[1024];
    spa_pod_builder b = SPA_POD_BUILDER_INIT(buffer, sizeof(buffer));
    spa_pod_frame frame;
    spa_pod_builder_push_object(&b, &frame, SPA_TYPE_OBJECT_Props, SPA_PARAM_Props);
    if (req.writeVolumes) {
        const int n = std::min<int>(req.volumes.size(), SPA_AUDIO_MAX_CHANNELS);
        if (n > 0) {
            float values[SPA_AUDIO_MAX_CHANNELS] = {};
            for (int i = 0; i < n; ++i) {
                // Clamp to PipeWire's documented [0.0, 1.0] linear
                // amplitude domain. Values > 1.0 are valid (boosts)
                // but uncommon; we leave that to the caller and only
                // refuse negatives so a stray -0 doesn't poison the
                // pod.
                values[i] = static_cast<float>(std::max<qreal>(0.0, req.volumes[i]));
            }
            spa_pod_builder_prop(&b, SPA_PROP_channelVolumes, 0);
            spa_pod_builder_array(&b, sizeof(float), SPA_TYPE_Float, static_cast<uint32_t>(n), values);
        }
    }
    if (req.writeMute) {
        spa_pod_builder_prop(&b, SPA_PROP_mute, 0);
        spa_pod_builder_bool(&b, req.muted);
    }
    auto* pod = static_cast<spa_pod*>(spa_pod_builder_pop(&b, &frame));
    if (!pod) {
        qCWarning(lcPipeWire) << "spa_pod_builder_pop returned null for node" << req.nodeId;
        return;
    }
    pw_node_set_param(node, SPA_PARAM_Props, 0, pod);
}

int PipeWireConnection::Private::dispatchDisconnect(struct spa_loop* loop, bool async, uint32_t seq, const void* data,
                                                    size_t size, void* user_data)
{
    Q_UNUSED(loop);
    Q_UNUSED(async);
    Q_UNUSED(seq);
    Q_UNUSED(data);
    Q_UNUSED(size);
    static_cast<Private*>(user_data)->doDisconnect();
    return 0;
}

void PipeWireConnection::Private::setConnected(bool value)
{
    if (connected.exchange(value) == value)
        return;
    Q_EMIT q->connectedChanged();
}

void PipeWireConnection::Private::setDaemonAvailable(bool value)
{
    if (daemonAvailable.exchange(value) == value)
        return;
    Q_EMIT q->daemonAvailableChanged();
}

void PipeWireConnection::Private::setDefaultSinkName(QString name)
{
    if (defaultSinkName == name)
        return;
    defaultSinkName = std::move(name);
    Q_EMIT q->defaultSinkNameChanged();
}

void PipeWireConnection::Private::setDefaultSourceName(QString name)
{
    if (defaultSourceName == name)
        return;
    defaultSourceName = std::move(name);
    Q_EMIT q->defaultSourceNameChanged();
}

void PipeWireConnection::Private::guiNodeAdded(quint32 id, QString mediaClass, QHash<QString, QString> props)
{
    if (guiNodes.contains(id))
        return;
    auto* node = new PwNode(id, mediaClass, q);
    node->applyInfo(props);
    guiNodes.insert(id, node);
    Q_EMIT q->nodeAdded(node);
}

void PipeWireConnection::Private::guiNodeInfo(quint32 id, QHash<QString, QString> props)
{
    auto it = guiNodes.find(id);
    if (it == guiNodes.end())
        return;
    it.value()->applyInfo(std::move(props));
}

void PipeWireConnection::Private::guiNodeProps(quint32 id, int channelCount, QList<qreal> volumes, bool muted)
{
    auto it = guiNodes.find(id);
    if (it == guiNodes.end())
        return;
    it.value()->applyProps(channelCount, std::move(volumes), muted);
}

void PipeWireConnection::Private::guiNodeRemoved(quint32 id)
{
    auto it = guiNodes.find(id);
    if (it == guiNodes.end())
        return;
    auto* node = it.value();
    guiNodes.erase(it);
    Q_EMIT q->nodeRemoved(node);
    node->deleteLater();
}

void PipeWireConnection::Private::guiNodesReset()
{
    if (guiNodes.isEmpty())
        return;
    const auto snapshot = guiNodes;
    guiNodes.clear();
    for (auto it = snapshot.cbegin(); it != snapshot.cend(); ++it) {
        Q_EMIT q->nodeRemoved(it.value());
        it.value()->deleteLater();
    }
}

PipeWireConnection::PipeWireConnection(QObject* parent)
    : QObject(parent)
    , d(std::make_unique<Private>(this))
{
    // QSignalSpy / QML queued connections need PwNode* known to Qt's
    // metatype system. The registration is idempotent so paying for it
    // in every constructor is fine.
    qRegisterMetaType<PwNode*>("PhosphorServicePipeWire::PwNode*");
    ensurePipeWireInit();
    d->thread.start();
    // Block until the worker has either created the loop or given up.
    // After this point `d->loop` is stable (either a valid pointer or
    // null with the thread already exiting) so the destructor's quit
    // path can't race against worker startup.
    d->startupReady.acquire();
}

PipeWireConnection::~PipeWireConnection()
{
    if (d->loop) {
        // pw_main_loop_quit is MT-safe (it routes through
        // pw_loop_signal_event internally) so we can call it from the
        // GUI thread directly. The loop thread's run() body picks up
        // the quit, tears down any remaining state via doDisconnect,
        // and exits.
        pw_main_loop_quit(d->loop);
    }
    if (!d->thread.wait(5000)) {
        qCWarning(lcPipeWire) << "PipeWire loop thread did not exit within 5s; terminating";
        d->thread.terminate();
        d->thread.wait();
    }
}

bool PipeWireConnection::isConnected() const
{
    return d->connected.load();
}

bool PipeWireConnection::isDaemonAvailable() const
{
    return d->daemonAvailable.load();
}

QList<PwNode*> PipeWireConnection::nodes() const
{
    return d->guiNodes.values();
}

QString PipeWireConnection::defaultSinkName() const
{
    return d->defaultSinkName;
}

QString PipeWireConnection::defaultSourceName() const
{
    return d->defaultSourceName;
}

void PipeWireConnection::connect()
{
    if (!d->thread.isRunning() || !d->loop)
        return;
    pw_loop_invoke(pw_main_loop_get_loop(d->loop), &Private::dispatchConnect, 0, nullptr, 0, false, d.get());
}

void PipeWireConnection::disconnect()
{
    if (!d->thread.isRunning() || !d->loop)
        return;
    pw_loop_invoke(pw_main_loop_get_loop(d->loop), &Private::dispatchDisconnect, 0, nullptr, 0, false, d.get());
}

void PipeWireConnection::writeVolumes(quint32 nodeId, const QList<qreal>& volumes)
{
    if (!d->thread.isRunning() || !d->loop)
        return;
    auto* req = new Private::ParamWriteRequest;
    req->owner = d.get();
    req->nodeId = nodeId;
    req->writeVolumes = true;
    req->volumes = volumes;
    pw_loop_invoke(pw_main_loop_get_loop(d->loop), &Private::dispatchParamWrite, 0, nullptr, 0, false, req);
}

void PipeWireConnection::writeMuted(quint32 nodeId, bool muted)
{
    if (!d->thread.isRunning() || !d->loop)
        return;
    auto* req = new Private::ParamWriteRequest;
    req->owner = d.get();
    req->nodeId = nodeId;
    req->writeMute = true;
    req->muted = muted;
    pw_loop_invoke(pw_main_loop_get_loop(d->loop), &Private::dispatchParamWrite, 0, nullptr, 0, false, req);
}

void PipeWireConnection::setDefaultSink(const QString& nodeName)
{
    if (!d->thread.isRunning() || !d->loop)
        return;
    auto* req = new Private::DefaultWriteRequest;
    req->owner = d.get();
    // Write the "configured" key so the choice persists across
    // restarts; WirePlumber promotes it into the runtime
    // default.audio.sink on the next reconcile.
    req->key = QStringLiteral("default.configured.audio.sink");
    req->nodeName = nodeName;
    pw_loop_invoke(pw_main_loop_get_loop(d->loop), &Private::dispatchDefaultWrite, 0, nullptr, 0, false, req);
}

void PipeWireConnection::setDefaultSource(const QString& nodeName)
{
    if (!d->thread.isRunning() || !d->loop)
        return;
    auto* req = new Private::DefaultWriteRequest;
    req->owner = d.get();
    req->key = QStringLiteral("default.configured.audio.source");
    req->nodeName = nodeName;
    pw_loop_invoke(pw_main_loop_get_loop(d->loop), &Private::dispatchDefaultWrite, 0, nullptr, 0, false, req);
}

} // namespace PhosphorServicePipeWire
