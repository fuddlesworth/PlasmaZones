// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include "pipewireconnection_p.h"

#include <PhosphorServicePipeWire/PwNode.h>

#include <QJsonDocument>
#include <QJsonObject>
#include <QVarLengthArray>

#include <spa/param/audio/raw.h>
#include <spa/param/props.h>
#include <spa/pod/iter.h>
#include <spa/pod/parser.h>

#include <mutex>

Q_LOGGING_CATEGORY(lcPipeWire, "phosphor.service.pipewire.connection")

// ── Cross-thread invariants ────────────────────────────────────────────────
//
// Every loop-thread callback that bounces state back to the GUI thread
// does so via QMetaObject::invokeMethod(d->q, [d, ...], QueuedConnection).
// The captured `d` (or `this` from a Private member) must outlive the
// queued lambda. The PipeWireConnection destructor guarantees that by:
//
//   1. Posting pw_main_loop_quit and waiting up to 5s for the loop
//      thread to exit. That gives the worker time to run its final
//      doDisconnect, which itself queues one last GUI lambda.
//   2. Calling QCoreApplication::sendPostedEvents(this, 0) to drain
//      every queued lambda already in the GUI event queue while
//      Private is still alive.
//   3. Calling QCoreApplication::removePostedEvents(this) to discard
//      anything still pending after the drain.
//
// So `d` is live for the duration of every lambda that ever gets a
// chance to run. The lambda-site comments below previously repeated
// this invariant at every callback; they now reference this block.

// Subclassing note: the destructor's drain runs AFTER the subclass
// destructors have replaced the vtable with the base PipeWireConnection
// one. A subclass that connects its OWN slots to nodeAdded /
// nodeRemoved / error and relies on subclass-side state in those slots
// will not see the final drain — by that point the subclass is gone.
// Observers should be external QObjects connected with
// Qt::QueuedConnection, not subclass methods. See LoopThread::run and
// the doDisconnect path it triggers.

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
    // Client, Node, ...). Compare directly against QLatin1String
    // literals via QStringView::compare so the hot path allocates
    // nothing — no toUtf8() temporary, no QByteArray, just a length
    // check + character-wise compare over the existing QString
    // backing storage. The class strings are pure ASCII so
    // QLatin1String comparison is byte-exact.
    //
    // Each candidate's length comes from the QLatin1String
    // constructor's strlen-equivalent at compile time; QStringView's
    // operator== is length-bounded so an embedded NUL in the
    // mediaClass (legal in a QString, plausible in a malformed
    // daemon-supplied value) cannot trick a shorter literal into a
    // false positive — the lengths differ and the compare short-
    // circuits.
    const QStringView view(mediaClass);
    static constexpr QLatin1String kAudioClasses[] = {
        QLatin1String("Audio/Sink"),
        QLatin1String("Audio/Source"),
        QLatin1String("Stream/Output/Audio"),
        QLatin1String("Stream/Input/Audio"),
    };
    for (const auto& candidate : kAudioClasses) {
        if (view == candidate)
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
        // Duplicate keys in a spa_dict are legal; QHash::insert keeps
        // the last value seen, which matches PipeWire's own
        // last-property-wins reading of a dict. A null value maps to an
        // empty string so a present-but-valueless key is distinguishable
        // from an absent one only by membership, not by value.
        out.insert(QString::fromUtf8(item->key), item->value ? QString::fromUtf8(item->value) : QString());
    }
    return out;
}

} // namespace detail

// LoopThread::run lives in pipewireconnection_lifecycle.cpp alongside
// the rest of the connect/disconnect state machine it drives. See the
// file-top doc block in pipewireconnection_lifecycle.cpp for the full
// inventory of definitions extracted into that TU.

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
    // present, handshake mid-flight". Pre-flip the atomic synchronously
    // on the loop thread (loop-thread truth) so isDaemonAvailable()
    // reports the correct value the moment the next caller looks; the
    // queued GUI lambda then only does the shadow-dedup + NOTIFY emit,
    // never touching the atomic. Lifetime: see top-of-file
    // "Cross-thread invariants" block.
    d->daemonAvailable.store(true, std::memory_order_release);
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
    // Reject the sentinel explicitly: when no sync is in flight,
    // pendingSyncSeq holds kNoPendingSync. PipeWire never emits a
    // negative seq, so this check is belt-and-braces — the equality
    // test already fails for any real seq because kNoPendingSync is
    // out of band — but stating it up front documents the invariant.
    if (id != PW_ID_CORE || d->pendingSyncSeq == kNoPendingSync || seq != d->pendingSyncSeq)
        return;
    qCDebug(lcPipeWire) << "pw_core done seq" << seq << "handshake complete";
    // Sync request satisfied. Clear the sentinel so a stray daemon-side
    // done event with the same seq cannot re-trigger this branch (e.g.
    // after a reconnect that re-uses seq=0 from a fresh pw_core_sync).
    d->pendingSyncSeq = kNoPendingSync;
    // Symmetric with onCoreError: synchronously flip `connected` on the
    // loop thread BEFORE queueing the GUI-side setConnected lambda.
    // doConnect's wedge-recovery check (`if (core && !connected.load())
    // doDisconnect()`) runs on the loop thread; without this store, a
    // re-issued connectToDaemon() landing between the queue-and-drain
    // window would see connected == false and tear down a freshly-
    // completed handshake. setConnected does a shadow-compare early
    // return (the GUI-thread `lastEmittedConnected` shadow, NOT the
    // atomic) so the queued mutation emits connectedChanged exactly
    // once even if a later loop-thread path pre-flips the atomic
    // before the GUI drains. Lifetime: see top-of-file "Cross-thread
    // invariants" block.
    d->connected.store(true, std::memory_order_release);
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
    // See the Cross-thread invariants block at the top of this file;
    // same L1/L2 pattern as setConnected / setDaemonAvailable applies
    // to both atomics flipped below. We deliberately do NOT tear
    // core/context/registry down from this callback: the next
    // caller-initiated connectToDaemon() observes the wedged state
    // (connected == false && core != nullptr && wedged == true) and
    // runs doDisconnect first, so recovery is automatic and the
    // teardown stays on the loop thread without us having to post
    // another pw_loop_invoke.
    d->connected.store(false, std::memory_order_release);
    d->daemonAvailable.store(false, std::memory_order_release);
    // Clear pendingSyncSeq so a stale `done` event echoing the original
    // syncRc can't pass onCoreDone's sentinel check and re-flip
    // `connected` back to true. PipeWire's daemon-teardown sequence can
    // emit error + done back-to-back, and the done arrives on this same
    // loop thread, so writing the sentinel here (loop-thread truth) is
    // race-free with the equality check in onCoreDone. Also flag the
    // wedged state so doConnect's recovery teardown can distinguish
    // "core listener died with an error" from "mid-handshake, sync
    // not yet acked".
    d->pendingSyncSeq = kNoPendingSync;
    d->wedged = true;
    QMetaObject::invokeMethod(
        d->q,
        [d, msg]() {
            d->setConnected(false);
            d->setDaemonAvailable(false);
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
            // qstrnlen caps at kMaxMetadataPayload, so `len` ==
            // kMaxMetadataPayload here means "at least that many
            // bytes" — the actual payload could be larger. Log both
            // the cap and the measured length so the warning is
            // self-describing without the reader having to look up
            // the constant.
            qCWarning(lcPipeWire) << "default-metadata value exceeds" << kMaxMetadataPayload << "bytes (measured len"
                                  << len << "); rejecting key" << keyStr;
            // Return BEFORE the invokeMethod block. Falling through
            // would queue setDefaultSinkName(QString{}) /
            // setDefaultSourceName(QString{}) and wipe the cached
            // default — a malicious/buggy daemon sending an oversized
            // payload could weaponise that to nuke our state.
            return 0;
        }
        const QJsonDocument doc = QJsonDocument::fromJson(QByteArray(value, len));
        if (!doc.isObject()) {
            // Well-sized payload but not a JSON object root: the
            // daemon sent something we don't understand. Bail BEFORE
            // the invokeMethod block — symmetric with the
            // oversize-payload branch above. Falling through would
            // queue setDefaultSinkName(QString{}) /
            // setDefaultSourceName(QString{}) and silently nuke the
            // cached default just because the payload shape drifted.
            // A malformed daemon push or schema-version mismatch
            // must not clobber observable state; the cached value
            // from the previous successful parse stays in force
            // until a parseable payload arrives.
            qCWarning(lcPipeWire) << "default-metadata value is not a JSON object for key" << keyStr
                                  << "; preserving cached value";
            return 0;
        }
        const QJsonValue nameVal = doc.object().value(QStringLiteral("name"));
        if (!nameVal.isString()) {
            // Object root but no `name` field, or `name` is not a
            // string. Same reasoning as the non-object branch
            // above: preserve the cached value rather than wipe it
            // on a malformed/unexpected schema.
            qCWarning(lcPipeWire) << "default-metadata value missing string 'name' field for key" << keyStr
                                  << "; preserving cached value";
            return 0;
        }
        nodeName = nameVal.toString();
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
    // `it == guiNodes.end()` check below is what makes that safe — a
    // node id observed on the loop thread is not guaranteed to still
    // exist on the GUI thread by the time the lambda fires. Same
    // contract for any other queued lambda that touches guiNodes.
    // Lifetime of `d`: see top-of-file "Cross-thread invariants" block.
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

// doConnect, doDisconnect, dispatchConnect, dispatchDisconnect live in
// pipewireconnection_lifecycle.cpp.

void PipeWireConnection::Private::setConnected(bool value)
{
    // GUI-side observability only. The atomic is loop-thread truth and
    // is written synchronously by the loop-thread callers (onCoreDone,
    // onCoreError, doConnect failure paths, doDisconnect); this setter
    // must NOT touch it.
    //
    // Why: a stale L1=setConnected(true) draining AFTER an Error
    // pre-flipped the atomic to false would re-write the atomic to true
    // and wedge doConnect's recovery check (`if (core &&
    // !connected.load()) doDisconnect()`) into reading true and
    // skipping the teardown — a permanent wedge until the next manual
    // disconnect. Keeping the atomic out of this setter makes that
    // race structurally impossible: late drains only emit NOTIFY,
    // never mutate cross-thread state.
    //
    // Dedup uses the GUI-thread `lastEmittedConnected` shadow so each
    // observable transition emits connectedChanged exactly once, even
    // when L1/L2 lambdas straddle a pre-flipped atomic.
    if (lastEmittedConnected == value)
        return;
    lastEmittedConnected = value;
    Q_EMIT q->connectedChanged();
}

void PipeWireConnection::Private::setDaemonAvailable(bool value)
{
    // Same atomic-vs-emit split as setConnected: the atomic is owned
    // by loop-thread paths (onCoreInfo pre-flips to true; doDisconnect
    // and doConnect failure paths pre-flip to false). This setter
    // only does GUI-side shadow-dedup and emits the NOTIFY signal.
    if (lastEmittedDaemonAvailable == value)
        return;
    lastEmittedDaemonAvailable = value;
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

void PipeWireConnection::Private::resetGuiSnapshot()
{
    // Reset every cached snapshot the public API exposes. Each setter
    // is its own dedupe (shadow-equal-skip for the booleans,
    // value-equal-skip for the strings) so calling these on
    // already-clean state is free and produces no spurious NOTIFY.
    //
    // Ordering matters: clear the strings FIRST, then flip the
    // booleans. Observers chaining off connectedChanged /
    // daemonAvailableChanged that re-read defaultSinkName /
    // defaultSourceName expect to see them already cleared by the
    // time the boolean transitions; the previous boolean-first order
    // gave them a window in which `connected == false` but the
    // default-name strings still held the previous session's values,
    // surfacing as ghost UI rows in observers that grab a fresh
    // snapshot inside the connectedChanged handler.
    setDefaultSinkName(QString());
    setDefaultSourceName(QString());
    setDaemonAvailable(false);
    setConnected(false);
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

// The PipeWireConnection ctor/dtor live in
// pipewireconnection_lifecycle.cpp.

bool PipeWireConnection::isConnected() const
{
    // memory_order_acquire pairs with the release-stores on the loop
    // thread (onCoreInfo, onCoreDone, onCoreError, doConnect failure
    // paths, doDisconnect). The default seq_cst would also be
    // correct, but using acquire matches the rest of the loop-thread
    // synchronisation pattern (see the `loop` atomic) and stays
    // consistent at compile-time review time.
    return d->connected.load(std::memory_order_acquire);
}

bool PipeWireConnection::isDaemonAvailable() const
{
    // Acquire for the same reason as isConnected — pair with the
    // release-stores on the loop thread.
    return d->daemonAvailable.load(std::memory_order_acquire);
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

// connectToDaemon / disconnectFromDaemon live in
// pipewireconnection_lifecycle.cpp.

} // namespace PhosphorServicePipeWire
