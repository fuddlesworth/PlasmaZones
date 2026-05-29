// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include "pipewireconnection_p.h"

#include <PhosphorServicePipeWire/PwNode.h>

#include <QCoreApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QVarLengthArray>

#include <spa/param/audio/raw.h>
#include <spa/param/props.h>
#include <spa/pod/iter.h>
#include <spa/pod/parser.h>
#include <spa/utils/result.h>

#include <cstring>
#include <mutex>

Q_LOGGING_CATEGORY(lcPipeWire, "phosphor.service.pipewire.connection")

namespace PhosphorServicePipeWire {

namespace detail {

namespace {
std::once_flag g_pwInitOnce;
} // namespace

void ensurePipeWireInit()
{
    std::call_once(g_pwInitOnce, [] {
        pw_init(nullptr, nullptr);
        // PwNode* needs to be registered with Qt's metatype system so
        // QSignalSpy and queued connections can carry it. The
        // registration is idempotent but takes a global mutex; pin it
        // to one-time-init alongside pw_init. Let Qt derive the name
        // from the type so an eventual rebrand of the namespace does
        // not require touching this string.
        qRegisterMetaType<PwNode*>();
    });
}

bool isAudioNodeClass(const QString& mediaClass)
{
    // Called on every registry global (Device, Port, Link, Factory,
    // Client, Node, ...). Compare UTF-8 bytes against fixed C literals
    // so the hot path pays one toUtf8 + a handful of strcmps rather
    // than constructing QString temporaries for each candidate.
    const QByteArray bytes = mediaClass.toUtf8();
    static constexpr const char* kAudioClasses[] = {
        "Audio/Sink",
        "Audio/Source",
        "Stream/Output/Audio",
        "Stream/Input/Audio",
    };
    for (const char* candidate : kAudioClasses) {
        if (std::strcmp(bytes.constData(), candidate) == 0)
            return true;
    }
    return false;
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

} // namespace detail

void PipeWireConnection::Private::LoopThread::run()
{
    pw_main_loop* createdLoop = pw_main_loop_new(nullptr);
    // Release-store so the GUI thread either sees null (the failure
    // path below) or the fully constructed loop pointer.
    owner->loop.store(createdLoop, std::memory_order_release);
    // Signal startup completion to the constructor whether or not the
    // loop was created. The constructor blocks on this semaphore so the
    // destructor never races against a worker that hasn't yet reached
    // pw_main_loop_new.
    owner->startupReady.release();
    if (!createdLoop) {
        qCWarning(lcPipeWire) << "pw_main_loop_new failed; PipeWire integration disabled";
        return;
    }
    // Blocks until pw_main_loop_quit fires or the loop exits on a
    // libpipewire-internal error.
    pw_main_loop_run(createdLoop);
    // Tear down any remaining loop-owned state on the loop thread, in
    // the correct order, before exit. doDisconnect is idempotent.
    owner->doDisconnect();
    pw_main_loop_destroy(createdLoop);
    owner->loop.store(nullptr, std::memory_order_release);
}

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

// kRegistryEvents lives in pipewireconnection_registry.cpp alongside the
// onRegistry* handlers it points at.

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
    qCDebug(lcPipeWire) << "pw_core done seq" << seq << "handshake complete";
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
    // Synchronously flip `connected` on the loop thread BEFORE queueing
    // the GUI-side emit. doConnect's wedge-recovery check
    // (`if (core && !connected.load(...)) doDisconnect()`) runs on the
    // loop thread, so it must observe the truthful value the moment the
    // next connect() request lands — even if the queued setConnected
    // lambda hasn't drained yet. Using exchange() here lets us capture
    // the prior value so the GUI-side queued lambda can still emit
    // connectedChanged exactly when the state genuinely transitioned
    // (setConnected would otherwise no-op because the atomic is already
    // false by the time it runs).
    //
    // We deliberately do NOT tear core/context/registry down from this
    // callback. The next caller-initiated connect() observes the wedged
    // state (connected == false && core != nullptr) and runs
    // doDisconnect first, so recovery is automatic and the teardown
    // stays on the loop thread without us having to post another
    // pw_loop_invoke.
    const bool wasConnected = d->connected.exchange(false, std::memory_order_acq_rel);
    QMetaObject::invokeMethod(
        d->q,
        [d, msg, wasConnected]() {
            if (wasConnected)
                Q_EMIT d->q->connectedChanged();
            Q_EMIT d->q->error(msg);
        },
        Qt::QueuedConnection);
}

// onRegistryGlobal / bindDefaultMetadata / bindAudioNode /
// onRegistryGlobalRemove live in pipewireconnection_registry.cpp.

int PipeWireConnection::Private::onDefaultMetadataProperty(void* data, uint32_t subject, const char* key,
                                                           const char* type, const char* value)
{
    Q_UNUSED(type);
    // WirePlumber's `default` metadata global only carries the system
    // defaults under subject PW_ID_CORE. Other subjects can legitimately
    // use the same key names for per-route or per-stream entries;
    // without this filter, those entries would clobber the cached
    // default-sink / -source names.
    if (subject != PW_ID_CORE)
        return 0;
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
        // shaped `{"name": "<node.name>"}`. Parse defensively: a
        // future schema bump could add fields and we should still pick
        // up the name. Cap the input at 64 KiB before strlen — the
        // metadata interface hands us an unbounded char* and a
        // malformed daemon payload could otherwise walk past memory
        // we don't own.
        constexpr qsizetype kMaxMetadataPayload = 64 * 1024;
        const qsizetype len = qstrnlen(value, kMaxMetadataPayload);
        if (len >= kMaxMetadataPayload) {
            qCWarning(lcPipeWire) << "default-metadata value exceeds" << kMaxMetadataPayload << "bytes; rejecting key"
                                  << keyStr;
            // Return BEFORE the invokeMethod block. Falling through
            // would queue setDefaultSinkName(QString{}) /
            // setDefaultSourceName(QString{}) and wipe the cached
            // default — a malicious/buggy daemon sending an oversized
            // payload could weaponise that to nuke our state.
            return 0;
        }
        const QJsonDocument doc = QJsonDocument::fromJson(QByteArray(value, len));
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
    const auto props = detail::propsFromDict(info->props);
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
    //
    // This is the volume-write hot path (every external mixer adjust,
    // every hotkey nudge). Use a stack-backed QVarLengthArray so the
    // typical 2-channel stereo case stays in inline storage and only
    // escalates to the heap when a 5.1+ surround sink shows up.
    int channelCount = 0;
    QVarLengthArray<qreal, 8> volumesStaging;
    bool muted = false;
    bool haveVolumes = false;
    bool haveMute = false;
    struct spa_pod_prop* prop = nullptr;
    SPA_POD_OBJECT_FOREACH(reinterpret_cast<const struct spa_pod_object*>(param), prop)
    {
        if (prop->key == SPA_PROP_channelVolumes) {
            float values[SPA_AUDIO_MAX_CHANNELS] = {};
            const uint32_t n = spa_pod_copy_array(&prop->value, SPA_TYPE_Float, values, SPA_AUDIO_MAX_CHANNELS);
            // n == 0 means the pod carried channelVolumes with an empty
            // array (malformed daemon payload). Treat it as "no volume
            // data" so we don't clobber a stereo node's cached channel
            // count + values with an empty list.
            if (n > 0) {
                channelCount = static_cast<int>(n);
                volumesStaging.resize(channelCount);
                for (uint32_t i = 0; i < n; ++i) {
                    volumesStaging[static_cast<int>(i)] = static_cast<qreal>(values[i]);
                }
                haveVolumes = true;
            }
        } else if (prop->key == SPA_PROP_mute) {
            bool m = false;
            if (spa_pod_get_bool(&prop->value, &m) == 0) {
                muted = m;
                haveMute = true;
            }
        }
    }

    if (!haveVolumes && !haveMute) {
        // Pod carried neither field; nothing observable changed.
        return;
    }

    // Materialise to QList only at the queued-emit boundary: the
    // queued lambda must own copyable, copy-constructible captures so
    // the cross-thread post can outlive this stack frame.
    QList<qreal> volumes;
    if (haveVolumes) {
        volumes.reserve(channelCount);
        for (int i = 0; i < channelCount; ++i)
            volumes.append(volumesStaging[i]);
    }

    // Explicit capture list rather than `[=]` so a future `[&]`
    // regression can't silently dangle. All captures are by-value: the
    // copies (`Private*` is a pointer, `quint32`/`bool`/`int` are
    // scalars, `QList<qreal>` is implicitly-shared) survive the cross-
    // thread post by design.
    //
    // Post-teardown safety: this lambda may sit in the GUI event queue
    // past a doDisconnect() that cleared guiNodes. The
    // `it == guiNodes.end()` check above is what makes that safe — a
    // node id observed on the loop thread is not guaranteed to still
    // exist on the GUI thread by the time the lambda fires. Same
    // contract for any other queued lambda that touches guiNodes.
    QMetaObject::invokeMethod(
        d->q,
        [d, nodeId, haveVolumes, haveMute, channelCount, volumes, muted]() {
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
    // Recovery path: an earlier onCoreError flipped `connected` to
    // false but left core/context/registry hanging. Tear them down
    // first so this reconnect starts from a clean slate. This is the
    // counterpart to the deliberate "don't teardown from the error
    // callback" choice in onCoreError — recovery happens here, on the
    // loop thread, exactly when the next caller asks to reconnect.
    if (core && !connected.load(std::memory_order_acquire))
        doDisconnect();
    if (core)
        return; // already connected (or mid-handshake)
    pw_main_loop* mainLoop = loop.load(std::memory_order_acquire);
    if (!mainLoop) {
        qCWarning(lcPipeWire) << "doConnect called before loop creation";
        return;
    }
    context = pw_context_new(pw_main_loop_get_loop(mainLoop), nullptr, 0);
    if (!context) {
        // Failure paths that leave `core` null must also reset
        // `connected`: an earlier onCoreError may have left it stale-
        // true on the GUI atomic until its queued lambda drained. Flip
        // it synchronously on the loop thread so isConnected() reports
        // the truth immediately, and queue setConnected(false) so the
        // GUI observers still see connectedChanged on the right thread.
        const bool wasConnected = connected.exchange(false, std::memory_order_acq_rel);
        QMetaObject::invokeMethod(
            q,
            [this, wasConnected]() {
                if (wasConnected)
                    Q_EMIT q->connectedChanged();
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
        const bool wasConnected = connected.exchange(false, std::memory_order_acq_rel);
        QMetaObject::invokeMethod(
            q,
            [this, wasConnected]() {
                if (wasConnected)
                    Q_EMIT q->connectedChanged();
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
    // completion marker. A negative return value is an errno code
    // from the loop layer (e.g. -EINVAL on a torn-down core), in
    // which case the handshake will never complete; surface it as a
    // hard error and leave pendingSyncSeq at zero so an unrelated
    // seq=0 done event can't be misread as our handshake.
    const int syncRc = pw_core_sync(core, PW_ID_CORE, 0);
    if (syncRc < 0) {
        pendingSyncSeq = 0;
        QMetaObject::invokeMethod(
            q,
            [this, syncRc]() {
                Q_EMIT q->error(QStringLiteral("pw_core_sync failed: %1").arg(syncRc));
            },
            Qt::QueuedConnection);
        return;
    }
    pendingSyncSeq = syncRc;
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
    defaultMetadataId = SPA_ID_INVALID;

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
    // Insert + announce BEFORE applyInfo. Observers that wire
    // infoChanged via nodeAdded would otherwise see an infoChanged
    // signal for a node they have not yet been told about, and either
    // ignore the first batch of properties or assert-trip.
    guiNodes.insert(id, node);
    Q_EMIT q->nodeAdded(node);
    node->applyInfo(std::move(props));
}

void PipeWireConnection::Private::guiNodeInfo(quint32 id, QHash<QString, QString> props)
{
    auto it = guiNodes.find(id);
    if (it == guiNodes.end())
        return;
    it.value()->applyInfo(std::move(props));
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
    detail::ensurePipeWireInit();
    d->thread.start();
    // Block until the worker has either created the loop or given up.
    // After this point `d->loop` is stable (either a valid pointer or
    // null with the thread already exiting) so the destructor's quit
    // path can't race against worker startup.
    d->startupReady.acquire();
}

PipeWireConnection::~PipeWireConnection()
{
    if (pw_main_loop* mainLoop = d->loop.load(std::memory_order_acquire)) {
        // pw_main_loop_quit is MT-safe (it routes through
        // pw_loop_signal_event internally) so we can call it from the
        // GUI thread directly. The loop thread's run() body picks up
        // the quit, tears down any remaining state via doDisconnect,
        // and exits. Quitting before the loop has started running is
        // also safe: pw_main_loop records the request and exits on
        // entry to pw_main_loop_run.
        pw_main_loop_quit(mainLoop);
    }
    if (!d->thread.wait(5000)) {
        // FATAL-BY-DESIGN: a stuck pw_main_loop_run means libpipewire
        // internal state cannot be reclaimed cleanly. We accept the
        // leak because the only path that gets here is process
        // shutdown — the OS reaps the address space immediately
        // after. A future reader should NOT try to soften this into
        // a recovery loop: the loop thread is wedged inside foreign
        // code, and forcibly resuming it would risk corrupting any
        // global libpipewire state still in use elsewhere.
        qCCritical(lcPipeWire) << "PipeWire loop thread did not exit within 5s; terminating "
                                  "(libpipewire state leaked; only acceptable at process shutdown)";
        d->thread.terminate();
        d->thread.wait();
    }
    // The loop thread's final doDisconnect — and every other loop-side
    // callback that ran before quit — posts QMetaObject::invokeMethod
    // lambdas targeting `this` via QueuedConnection. By the time the
    // destructor runs they are sitting in the GUI thread's event
    // queue, and observers wired to nodeAdded / nodeRemoved have not
    // yet seen them.
    //
    // First flush: deliver every queued cross-thread post targeting
    // `this` so observers (PwNodeModel, KCM widgets, ...) receive the
    // final round of add/remove signals and drop any pointers to
    // PwNode children that are about to be destroyed. Then drop
    // anything else still queued for `this` so the GUI event loop
    // doesn't dispatch into a half-destroyed object after we return.
    QCoreApplication::sendPostedEvents(this, 0);
    QCoreApplication::removePostedEvents(this);
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
    pw_main_loop* mainLoop = d->loop.load(std::memory_order_acquire);
    if (!d->thread.isRunning() || !mainLoop)
        return;
    // connect/disconnect bypass submitLoopRequest because they carry no
    // payload (no heap request, no ownership handoff). A negative rc
    // means the loop refused the post; surface it as a warning so the
    // failure isn't silently dropped, but keep the call shape simple.
    const int rc =
        pw_loop_invoke(pw_main_loop_get_loop(mainLoop), &Private::dispatchConnect, 0, nullptr, 0, false, d.get());
    if (rc < 0)
        qCWarning(lcPipeWire) << "pw_loop_invoke failed for connect rc" << rc;
}

void PipeWireConnection::disconnect()
{
    pw_main_loop* mainLoop = d->loop.load(std::memory_order_acquire);
    if (!d->thread.isRunning() || !mainLoop)
        return;
    const int rc =
        pw_loop_invoke(pw_main_loop_get_loop(mainLoop), &Private::dispatchDisconnect, 0, nullptr, 0, false, d.get());
    if (rc < 0)
        qCWarning(lcPipeWire) << "pw_loop_invoke failed for disconnect rc" << rc;
}

} // namespace PhosphorServicePipeWire
