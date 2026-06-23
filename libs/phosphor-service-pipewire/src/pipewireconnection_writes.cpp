// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include "pipewireconnection_p.h"

#include <QJsonDocument>
#include <QJsonObject>

#include <spa/param/audio/raw.h>
#include <spa/param/props.h>
#include <spa/pod/builder.h>

#include <algorithm>
#include <cmath>
#include <memory>

namespace PhosphorServicePipeWire {

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
    // Reject empty-string node names: WirePlumber's interpretation of
    // {"name": ""} is undocumented (some versions treat it as "clear
    // default", others reject the payload). Callers wanting an explicit
    // "no default" should land their own clear-API once we define one;
    // until then, drop the malformed write rather than guess.
    if (req.nodeName.isEmpty()) {
        qCDebug(lcPipeWire) << "default-write with empty node name; dropping" << req.key;
        return;
    }
    // WirePlumber stores defaults as Spa:String:JSON values shaped
    // `{"name": "<node.name>"}`. Build the JSON via QJsonDocument so
    // the encoding (quoting, escaping) is consistent with the
    // metadata reader path.
    QJsonObject obj;
    obj.insert(QStringLiteral("name"), req.nodeName);
    const QByteArray payload = QJsonDocument(obj).toJson(QJsonDocument::Compact);
    // Hold the key's UTF-8 bytes in a named local: passing
    // req.key.toUtf8().constData() inline would dangle if a future edit
    // ever hoisted the call out of this full-expression.
    const QByteArray keyUtf8 = req.key.toUtf8();
    const int rc = pw_metadata_set_property(reinterpret_cast<pw_metadata*>(defaultMetadata), 0, keyUtf8.constData(),
                                            "Spa:String:JSON", payload.constData());
    // Fire-and-forget per the public contract (the result surfaces via
    // the metadata-property echo signal, not a return), but a negative
    // errno means the write never reached the daemon — log it at debug
    // for parity with the loop-dispatch failure path so a dropped
    // default-write isn't completely invisible.
    if (rc < 0) {
        qCDebug(lcPipeWire) << "pw_metadata_set_property failed for key" << req.key << "rc" << rc;
    }
}

template<typename Req>
bool PipeWireConnection::Private::submitLoopRequest(std::unique_ptr<Req> req,
                                                    int (*dispatcher)(struct spa_loop*, bool, uint32_t, const void*,
                                                                      size_t, void*),
                                                    const char* label)
{
    // Centralised guard: thread not started, loop not yet created,
    // or loop already destroyed → silently drop. The public API
    // contract is fire-and-forget; surfacing these as errors would
    // produce noise during shutdown ordering.
    if (!thread.isRunning())
        return false;
    // pw_loop_invoke returns a sequence number (>= 0) on async
    // success or the dispatcher's return value on sync; only a
    // negative value signals a queue-side failure where the
    // dispatcher won't fire.
    //
    // Sync-dispatch double-free hazard: pw_loop_invoke called with
    // block=false from a thread that IS the loop's own thread runs
    // the dispatcher SYNCHRONOUSLY before returning. The dispatcher
    // takes ownership via its own unique_ptr from `user_data` and
    // deletes the request when it returns. If we still held the
    // unique_ptr across the invoke (via req.get() and a later
    // req.release()), the local unique_ptr would also try to delete
    // the same pointer on scope exit, producing a double-free. Our
    // public contract documents GUI-thread-only callers, but a
    // misuse — or a future reentrant call path — must not corrupt
    // memory. Release ownership to a raw pointer BEFORE the invoke
    // and reclaim ONLY on the queue-failure path (rc < 0), which
    // signals the dispatcher will not fire. Once rc >= 0, ownership
    // belongs to the dispatcher regardless of sync vs async
    // delivery; the local raw pointer is intentionally leaked from
    // this stack frame's point of view.
    //
    // loopMutex scope: hold the lock across the load AND the
    // pw_loop_invoke so a spontaneous worker-thread loop exit
    // (libpipewire-internal error, daemon kill mid-run) cannot
    // destroy the loop between our load and our invoke. The worker
    // holds the same mutex across its destroy+null-store in
    // LoopThread::run, so either we see a live loop and the invoke
    // races safely against the eventual teardown, or we see null
    // and bail. The reclaim-on-failure happens OUTSIDE the lock —
    // pw_loop_invoke has already returned by the time we get rc,
    // and the delete touches only local heap state. See the
    // destructor (pipewireconnection_lifecycle.cpp) for the
    // canonical statement of the TOCTOU window this closes.
    Req* raw = req.release();
    int rc;
    {
        QMutexLocker locker(&loopMutex);
        pw_main_loop* mainLoop = loop.load(std::memory_order_acquire);
        if (!mainLoop) {
            // No live loop — reclaim and drop. The dispatcher will
            // never fire, so we own the request.
            delete raw;
            return false;
        }
        rc = pw_loop_invoke(pw_main_loop_get_loop(mainLoop), dispatcher, 0, nullptr, 0, false, raw);
    }
    if (rc < 0) {
        // Queue-side failure: dispatcher did not fire and never
        // will. Reclaim ownership and drop. On rc >= 0 (sync or
        // async), ownership has transferred to the dispatcher.
        qCWarning(lcPipeWire) << "pw_loop_invoke failed for" << label << "rc" << rc;
        delete raw;
        return false;
    }
    return true;
}

void PipeWireConnection::Private::doParamWrite(const ParamWriteRequest& req)
{
    // If neither field is set, there's nothing to push to the daemon.
    // Skip the entire pod build instead of emitting an empty Props
    // pod that the daemon would have to parse and ignore.
    const int volumeCount = req.writeVolumes ? std::min<int>(req.volumes.size(), SPA_AUDIO_MAX_CHANNELS) : 0;
    // Surface truncation explicitly: a caller handing us more
    // channels than SPA can carry is almost certainly a bug at the
    // call site (wrong channel count, swapped sink, stale cached
    // layout) and silently dropping the tail would let it ship. The
    // pod build still proceeds with the truncated array so the
    // observable channels still update; the qCDebug just makes the
    // truncation visible to anyone running with the category enabled.
    if (req.writeVolumes && req.volumes.size() > SPA_AUDIO_MAX_CHANNELS) {
        qCDebug(lcPipeWire) << "writeVolumes truncating channel count from" << req.volumes.size() << "to"
                            << SPA_AUDIO_MAX_CHANNELS << "for node" << req.nodeId;
    }
    if (volumeCount == 0 && !req.writeMuted)
        return;

    auto it = loopNodes.find(req.nodeId);
    if (it == loopNodes.end() || !it->second || !it->second->proxy) {
        qCDebug(lcPipeWire) << "param write for unknown / detached node" << req.nodeId;
        return;
    }
    auto* node = reinterpret_cast<pw_node*>(it->second->proxy);

    // Build a Props pod containing only the field(s) we're updating.
    // The buffer size budget: 1KiB is enough for a single Props
    // object with up to SPA_AUDIO_MAX_CHANNELS channel-volume floats
    // (the array dominates the size) plus a bounded constant for the
    // SPA bookkeeping overhead (object header, the channelVolumes
    // property header, and the optional mute property header — all
    // three are fixed-size and fit comfortably in a 256-byte slack
    // budget). SPA_AUDIO_MAX_CHANNELS is 64 today, so the worst case
    // is well under 768 bytes. Pin that arithmetic at compile time so
    // a future SPA bump can't silently overflow the stack buffer.
    static_assert(SPA_AUDIO_MAX_CHANNELS * sizeof(float) + 256 <= 1024,
                  "raise pod buffer: SPA_AUDIO_MAX_CHANNELS grew");
    uint8_t buffer[1024];
    spa_pod_builder b = SPA_POD_BUILDER_INIT(buffer, sizeof(buffer));
    spa_pod_frame frame;
    spa_pod_builder_push_object(&b, &frame, SPA_TYPE_OBJECT_Props, SPA_PARAM_Props);
    if (volumeCount > 0) {
        float values[SPA_AUDIO_MAX_CHANNELS] = {};
        for (int i = 0; i < volumeCount; ++i) {
            // Clamp to PipeWire's documented [0.0, 1.0] linear
            // amplitude domain. Values > 1.0 are valid (boosts)
            // but uncommon; we leave that to the caller and only
            // refuse negatives so a stray -0 doesn't poison the
            // pod. NaN/Inf coerced to 0.0: std::max(0.0, NaN) is
            // implementation-defined (commonly returns NaN), and a
            // NaN amplitude reaching the daemon produces undefined
            // mixing behavior (silence on some backends, glitches
            // on others).
            const qreal raw = req.volumes[i];
            values[i] = std::isfinite(raw) ? static_cast<float>(std::max<qreal>(0.0, raw)) : 0.0f;
        }
        spa_pod_builder_prop(&b, SPA_PROP_channelVolumes, 0);
        spa_pod_builder_array(&b, sizeof(float), SPA_TYPE_Float, static_cast<uint32_t>(volumeCount), values);
    }
    if (req.writeMuted) {
        spa_pod_builder_prop(&b, SPA_PROP_mute, 0);
        spa_pod_builder_bool(&b, req.muted);
    }
    auto* pod = static_cast<spa_pod*>(spa_pod_builder_pop(&b, &frame));
    if (!pod) {
        qCWarning(lcPipeWire) << "spa_pod_builder_pop returned null for node" << req.nodeId;
        return;
    }
    const int rc = pw_node_set_param(node, SPA_PARAM_Props, 0, pod);
    // Fire-and-forget per the public contract (the result surfaces via
    // propsChanged once the daemon echoes the param), but a negative
    // errno means the param never landed — log it at debug so a write
    // rejected by a stale proxy or busy daemon isn't completely silent.
    if (rc < 0) {
        qCDebug(lcPipeWire) << "pw_node_set_param failed for node" << req.nodeId << "rc" << rc;
    }
}

void PipeWireConnection::writeVolumes(quint32 nodeId, const QList<qreal>& volumes)
{
    // Reject empty volume lists at the API boundary. An empty list
    // carries no observable channel update; dispatching it would wake
    // the loop thread, hand a ParamWriteRequest through pw_loop_invoke,
    // and have doParamWrite early-return on volumeCount == 0 — all of
    // it pure overhead. Bail synchronously and log so a buggy caller
    // (stale model snapshot, race against device removal) shows up in
    // the trace.
    if (volumes.isEmpty()) {
        qCDebug(lcPipeWire) << "writeVolumes called with empty volume list for node" << nodeId << "; ignoring";
        return;
    }
    auto req = std::unique_ptr<Private::ParamWriteRequest>(new Private::ParamWriteRequest{
        .owner = d.get(),
        .nodeId = nodeId,
        .writeVolumes = true,
        .writeMuted = false,
        .volumes = volumes,
        .muted = false,
    });
    d->submitLoopRequest(std::move(req), &Private::dispatchParamWrite, "writeVolumes");
}

void PipeWireConnection::writeMuted(quint32 nodeId, bool muted)
{
    auto req = std::unique_ptr<Private::ParamWriteRequest>(new Private::ParamWriteRequest{
        .owner = d.get(),
        .nodeId = nodeId,
        .writeVolumes = false,
        .writeMuted = true,
        .volumes = {},
        .muted = muted,
    });
    d->submitLoopRequest(std::move(req), &Private::dispatchParamWrite, "writeMuted");
}

void PipeWireConnection::setDefaultSink(const QString& nodeName)
{
    // Write the "configured" key so the choice persists across
    // restarts; WirePlumber promotes it into the runtime
    // default.audio.sink on the next reconcile.
    auto req = std::unique_ptr<Private::DefaultWriteRequest>(new Private::DefaultWriteRequest{
        .owner = d.get(),
        .key = QStringLiteral("default.configured.audio.sink"),
        .nodeName = nodeName,
    });
    d->submitLoopRequest(std::move(req), &Private::dispatchDefaultWrite, "setDefaultSink");
}

void PipeWireConnection::setDefaultSource(const QString& nodeName)
{
    auto req = std::unique_ptr<Private::DefaultWriteRequest>(new Private::DefaultWriteRequest{
        .owner = d.get(),
        .key = QStringLiteral("default.configured.audio.source"),
        .nodeName = nodeName,
    });
    d->submitLoopRequest(std::move(req), &Private::dispatchDefaultWrite, "setDefaultSource");
}

// submitLoopRequest is instantiated implicitly for ParamWriteRequest
// and DefaultWriteRequest from the four call sites above (writeVolumes,
// writeMuted, setDefaultSink, setDefaultSource), all in this TU, so no
// explicit instantiation is needed. If a future refactor moves a caller
// into a different TU, add the instantiation there at that time.

} // namespace PhosphorServicePipeWire
