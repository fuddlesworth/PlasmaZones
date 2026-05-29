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
    // object with up to ~64 channel-volume floats plus the SPA
    // bookkeeping overhead. SPA_AUDIO_MAX_CHANNELS is 64 today, so
    // the worst case is well under 512 bytes.
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
    if (!d->thread.isRunning() || !d->loop)
        return;
    auto req = std::make_unique<Private::ParamWriteRequest>();
    req->owner = d.get();
    req->nodeId = nodeId;
    req->writeVolumes = true;
    req->volumes = volumes;
    // pw_loop_invoke returns a sequence number (positive) on async
    // success or the invoked function's return value on sync; only a
    // negative value signals a queue-side failure where the dispatcher
    // won't fire. Release ownership to the dispatcher unless we hit
    // that rare negative case (unique_ptr cleans up on scope exit).
    const int rc =
        pw_loop_invoke(pw_main_loop_get_loop(d->loop), &Private::dispatchParamWrite, 0, nullptr, 0, false, req.get());
    if (rc >= 0) {
        (void)req.release();
    } else {
        qCWarning(lcPipeWire) << "pw_loop_invoke failed for writeVolumes" << nodeId << "rc" << rc;
    }
}

void PipeWireConnection::writeMuted(quint32 nodeId, bool muted)
{
    if (!d->thread.isRunning() || !d->loop)
        return;
    auto req = std::make_unique<Private::ParamWriteRequest>();
    req->owner = d.get();
    req->nodeId = nodeId;
    req->writeMuted = true;
    req->muted = muted;
    const int rc =
        pw_loop_invoke(pw_main_loop_get_loop(d->loop), &Private::dispatchParamWrite, 0, nullptr, 0, false, req.get());
    if (rc >= 0) {
        (void)req.release();
    } else {
        qCWarning(lcPipeWire) << "pw_loop_invoke failed for writeMuted" << nodeId << "rc" << rc;
    }
}

void PipeWireConnection::setDefaultSink(const QString& nodeName)
{
    if (!d->thread.isRunning() || !d->loop)
        return;
    auto req = std::make_unique<Private::DefaultWriteRequest>();
    req->owner = d.get();
    // Write the "configured" key so the choice persists across
    // restarts; WirePlumber promotes it into the runtime
    // default.audio.sink on the next reconcile.
    req->key = QStringLiteral("default.configured.audio.sink");
    req->nodeName = nodeName;
    const int rc =
        pw_loop_invoke(pw_main_loop_get_loop(d->loop), &Private::dispatchDefaultWrite, 0, nullptr, 0, false, req.get());
    if (rc >= 0) {
        (void)req.release();
    } else {
        qCWarning(lcPipeWire) << "pw_loop_invoke failed for setDefaultSink" << nodeName << "rc" << rc;
    }
}

void PipeWireConnection::setDefaultSource(const QString& nodeName)
{
    if (!d->thread.isRunning() || !d->loop)
        return;
    auto req = std::make_unique<Private::DefaultWriteRequest>();
    req->owner = d.get();
    req->key = QStringLiteral("default.configured.audio.source");
    req->nodeName = nodeName;
    const int rc =
        pw_loop_invoke(pw_main_loop_get_loop(d->loop), &Private::dispatchDefaultWrite, 0, nullptr, 0, false, req.get());
    if (rc >= 0) {
        (void)req.release();
    } else {
        qCWarning(lcPipeWire) << "pw_loop_invoke failed for setDefaultSource" << nodeName << "rc" << rc;
    }
}

} // namespace PhosphorServicePipeWire
