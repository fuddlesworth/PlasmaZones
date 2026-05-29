// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include "pipewireconnection_p.h"

#include <QJsonDocument>
#include <QJsonObject>

#include <spa/param/audio/raw.h>
#include <spa/param/props.h>
#include <spa/pod/builder.h>

#include <algorithm>
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
    pw_main_loop* mainLoop = loop.load(std::memory_order_acquire);
    if (!thread.isRunning() || !mainLoop)
        return false;
    // pw_loop_invoke returns a sequence number (>= 0) on async
    // success or the dispatcher's return value on sync; only a
    // negative value signals a queue-side failure where the
    // dispatcher won't fire. Release ownership to the dispatcher on
    // success, log + drop on failure (unique_ptr cleans up on
    // scope exit).
    const int rc = pw_loop_invoke(pw_main_loop_get_loop(mainLoop), dispatcher, 0, nullptr, 0, false, req.get());
    if (rc < 0) {
        qCWarning(lcPipeWire) << "pw_loop_invoke failed for" << label << "rc" << rc;
        return false;
    }
    (void)req.release();
    return true;
}

void PipeWireConnection::Private::doParamWrite(const ParamWriteRequest& req)
{
    // If neither field is set, there's nothing to push to the daemon.
    // Skip the entire pod build instead of emitting an empty Props
    // pod that the daemon would have to parse and ignore.
    const int volumeCount = req.writeVolumes ? std::min<int>(req.volumes.size(), SPA_AUDIO_MAX_CHANNELS) : 0;
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
            // pod.
            values[i] = static_cast<float>(std::max<qreal>(0.0, req.volumes[i]));
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
    pw_node_set_param(node, SPA_PARAM_Props, 0, pod);
}

void PipeWireConnection::writeVolumes(quint32 nodeId, const QList<qreal>& volumes)
{
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

// Forward-compatibility instantiations only. Every current caller
// (writeVolumes / writeMuted / setDefaultSink / setDefaultSource)
// lives in this TU, so the compiler already instantiates
// submitLoopRequest implicitly for ParamWriteRequest and
// DefaultWriteRequest from the call sites above; these explicit
// instantiations are functionally dead code today.
//
// They are kept (not deleted) so a later refactor that moves a caller
// into a different TU — e.g. splitting writeVolumes/writeMuted out
// into a dedicated nodewrite TU — surfaces the now-needed
// instantiation as already-present here rather than as a confusing
// undefined-reference link error. Cost: two emitted symbol bodies;
// benefit: a refactor-time landmine defused. If a future cleanup
// audits truly-dead-code aggressively, drop these and accept that the
// refactorer will get a link error pointing them at this file.
template bool PipeWireConnection::Private::submitLoopRequest<PipeWireConnection::Private::ParamWriteRequest>(
    std::unique_ptr<PipeWireConnection::Private::ParamWriteRequest>,
    int (*)(struct spa_loop*, bool, uint32_t, const void*, size_t, void*), const char*);
template bool PipeWireConnection::Private::submitLoopRequest<PipeWireConnection::Private::DefaultWriteRequest>(
    std::unique_ptr<PipeWireConnection::Private::DefaultWriteRequest>,
    int (*)(struct spa_loop*, bool, uint32_t, const void*, size_t, void*), const char*);

} // namespace PhosphorServicePipeWire
