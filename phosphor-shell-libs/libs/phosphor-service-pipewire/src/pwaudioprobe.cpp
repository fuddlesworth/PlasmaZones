// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
#include <PhosphorServicePipeWire/PwAudioProbe.h>
#include "pipewireconnection_p.h"
#include <QElapsedTimer>
#include <QTimer>
#include <pipewire/thread-loop.h>
#include <spa/param/audio/format-utils.h>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <numbers>
namespace PhosphorServicePipeWire {
class PwAudioProbe::Private
{
public:
    PwAudioProbe* owner = nullptr;
    pw_thread_loop* loop = nullptr;
    pw_stream* stream = nullptr;
    bool capture = false;
    bool running = false;
    bool started = false;
    qreal level = 0;
    QTimer poll;
    QElapsedTimer elapsed;
    std::atomic<quint64> generation{0};
    std::atomic<float> peak{0};
    std::atomic<bool> received{false};
    std::atomic<bool> finished{false};
    quint64 frames = 0; // Process thread only, reset before that thread starts.

    static void process(void* data)
    {
        auto* self = static_cast<Private*>(data);
        auto* buffer = pw_stream_dequeue_buffer(self->stream);
        if (!buffer)
            return;
        auto* spa = buffer->buffer;
        if (!spa || spa->n_datas == 0 || !spa->datas[0].data || !spa->datas[0].chunk) {
            pw_stream_queue_buffer(self->stream, buffer);
            return;
        }
        auto& block = spa->datas[0];
        if (self->capture) {
            const auto offset = std::min(block.chunk->offset, block.maxsize);
            const auto count = std::min(block.chunk->size, block.maxsize - offset) / sizeof(float);
            const auto* bytes = static_cast<const char*>(block.data) + offset;
            float peak = 0;
            for (size_t i = 0; i < count; ++i) {
                float sample;
                std::memcpy(&sample, bytes + i * sizeof(float), sizeof(float));
                if (std::isfinite(sample))
                    peak = std::max(peak, std::abs(sample));
            }
            self->peak.store(std::min(peak, 1.0f), std::memory_order_relaxed);
            self->received.store(true, std::memory_order_relaxed);
        } else {
            auto* samples = static_cast<float*>(block.data);
            const auto capacity = block.maxsize / sizeof(float);
            const auto count = buffer->requested ? std::min<quint64>(buffer->requested, capacity) : capacity;
            constexpr quint64 duration = 21600; // 450 ms at 48 kHz.
            for (quint64 i = 0; i < count; ++i, ++self->frames) {
                const double time = static_cast<double>(self->frames) / 48000.0;
                const double envelope = std::clamp(std::min(time / 0.025, (0.45 - time) / 0.08), 0.0, 1.0);
                samples[i] = static_cast<float>(0.08 * envelope * std::sin(2 * std::numbers::pi * 660 * time));
            }
            block.chunk->offset = 0;
            block.chunk->stride = sizeof(float);
            block.chunk->size = static_cast<uint32_t>(count * sizeof(float));
            buffer->size = count;
            self->received.store(true, std::memory_order_relaxed);
            if (self->frames >= duration)
                self->finished.store(true, std::memory_order_relaxed);
        }
        pw_stream_queue_buffer(self->stream, buffer);
    }
    static void stateChanged(void* data, pw_stream_state oldState, pw_stream_state state, const char* error)
    {
        auto* self = static_cast<Private*>(data);
        if (state != PW_STREAM_STATE_ERROR
            && !(state == PW_STREAM_STATE_UNCONNECTED && oldState >= PW_STREAM_STATE_CONNECTING))
            return;
        if (state == PW_STREAM_STATE_ERROR)
            qCWarning(lcPipeWire) << "Audio test failed:" << error;
        const auto generation = self->generation.load();
        QMetaObject::invokeMethod(
            self->owner,
            [self, generation] {
                if (generation != self->generation.load())
                    return;
                self->owner->stop();
                Q_EMIT self->owner->error(
                    PwAudioProbe::tr("The audio test disconnected. Check the device and try again."));
            },
            Qt::QueuedConnection);
    }
    void release()
    {
        ++generation;
        poll.stop();
        if (loop && started)
            pw_thread_loop_stop(loop);
        if (stream)
            pw_stream_destroy(stream);
        if (loop)
            pw_thread_loop_destroy(loop);
        // Destroying a stream can itself queue an UNCONNECTED callback.
        // Invalidate those teardown events as well as pre-stop callbacks.
        ++generation;
        stream = nullptr;
        loop = nullptr;
        started = false;
        running = false;
        peak.store(0);
        received.store(false);
        finished.store(false);
    }
};
PwAudioProbe::PwAudioProbe(QObject* parent)
    : QObject(parent)
    , d(std::make_unique<Private>())
{
    d->owner = this;
    d->poll.setInterval(33);
    connect(&d->poll, &QTimer::timeout, this, [this] {
        if (d->finished.load() || (d->capture && d->elapsed.elapsed() > 30000)) {
            stop();
            return;
        }
        if (!d->received.load() && d->elapsed.elapsed() > 5000) {
            stop();
            Q_EMIT error(tr("The audio device didn’t start. Check its connection and try again."));
            return;
        }
        const qreal value = d->capture ? std::max<qreal>(d->peak.exchange(0), d->level * 0.7) : 0;
        if (!qFuzzyCompare(d->level, value)) {
            d->level = value;
            Q_EMIT levelChanged();
        }
    });
}
PwAudioProbe::~PwAudioProbe()
{
    d->release();
}
bool PwAudioProbe::listening() const
{
    return d->running && d->capture;
}
bool PwAudioProbe::playing() const
{
    return d->running && !d->capture;
}
qreal PwAudioProbe::level() const
{
    return d->level;
}
void PwAudioProbe::stop()
{
    const bool wasRunning = d->running;
    d->release();
    if (d->level != 0) {
        d->level = 0;
        Q_EMIT levelChanged();
    }
    if (wasRunning)
        Q_EMIT activeChanged();
}
void PwAudioProbe::startInputTest(const QString& nodeName)
{
    start(nodeName, true);
}
void PwAudioProbe::playTestSound(const QString& nodeName)
{
    start(nodeName, false);
}
void PwAudioProbe::start(const QString& nodeName, bool capture)
{
    stop();
    if (nodeName.isEmpty()) {
        Q_EMIT error(tr("Choose an audio device first."));
        return;
    }
    detail::ensurePipeWireInit();
    d->capture = capture;
    d->frames = 0;
    d->loop = pw_thread_loop_new("PhosphorAudioTest", nullptr);
    if (!d->loop) {
        Q_EMIT error(tr("Couldn’t start the audio test."));
        return;
    }
    static const pw_stream_events events = [] {
        pw_stream_events result{};
        result.version = PW_VERSION_STREAM_EVENTS;
        result.state_changed = Private::stateChanged;
        result.process = Private::process;
        return result;
    }();
    const auto target = nodeName.toUtf8();
    auto* properties = pw_properties_new(
        PW_KEY_MEDIA_TYPE, "Audio", PW_KEY_MEDIA_CATEGORY, capture ? "Capture" : "Playback", PW_KEY_MEDIA_ROLE,
        capture ? "Communication" : "Notification", PW_KEY_APP_NAME, "Phosphor audio test", PW_KEY_TARGET_OBJECT,
        target.constData(), PW_KEY_NODE_DONT_RECONNECT, "true", "node.dont-fallback", "true", "stream.dont-remix",
        "false", "node.stream.restore-props", "false", nullptr);
    d->stream =
        pw_stream_new_simple(pw_thread_loop_get_loop(d->loop), "Phosphor audio test", properties, &events, d.get());
    if (!d->stream) {
        stop();
        Q_EMIT error(tr("Couldn’t start the audio test."));
        return;
    }
    uint8_t bytes[1024];
    auto builder = SPA_POD_BUILDER_INIT(bytes, sizeof(bytes));
    spa_audio_info_raw format{};
    format.format = SPA_AUDIO_FORMAT_F32;
    format.rate = 48000;
    format.channels = 1;
    format.position[0] = SPA_AUDIO_CHANNEL_MONO;
    const spa_pod* params[] = {spa_format_audio_raw_build(&builder, SPA_PARAM_EnumFormat, &format)};
    const auto flags = static_cast<pw_stream_flags>(PW_STREAM_FLAG_AUTOCONNECT | PW_STREAM_FLAG_MAP_BUFFERS
                                                    | PW_STREAM_FLAG_RT_PROCESS);
    if (pw_stream_connect(d->stream, capture ? PW_DIRECTION_INPUT : PW_DIRECTION_OUTPUT, PW_ID_ANY, flags, params, 1)
        < 0) {
        stop();
        Q_EMIT error(tr("Couldn’t connect the audio test to this device."));
        return;
    }
    if (pw_thread_loop_start(d->loop) < 0) {
        stop();
        Q_EMIT error(tr("Couldn’t start the audio test."));
        return;
    }
    d->started = true;
    d->running = true;
    d->elapsed.start();
    d->poll.start();
    Q_EMIT activeChanged();
}
}
