// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
#include <PhosphorShell/SystemStats.h>
#include <PhosphorShell/SystemStatsReader.h>
#include <algorithm>
#include <limits>
#include <utility>

namespace PhosphorShell {
SystemStats::SystemStats(QObject* parent)
    : QObject(parent)
    , m_worker(new QObject)
{
    m_clock.start();
    m_worker->moveToThread(&m_thread);
    connect(&m_thread, &QThread::finished, m_worker, &QObject::deleteLater);
    connect(&m_timer, &QTimer::timeout, this, &SystemStats::requestSample);
    m_thread.setObjectName(QStringLiteral("SystemStatsCollector"));
    m_thread.start();
}
SystemStats::~SystemStats()
{
    m_timer.stop();
    // No outstanding task may dereference this after destruction. Collection
    // only reads local kernel files and the driver's in-process query API.
    m_thread.quit();
    m_thread.wait();
}
void SystemStats::watch(QObject* consumer, bool enabled)
{
    if (!consumer || m_consumers.contains(consumer) == enabled)
        return;
    const bool wasActive = active();
    if (enabled) {
        m_consumers.insert(consumer, connect(consumer, &QObject::destroyed, this, [this, consumer] {
                               watch(consumer, false);
                           }));
    } else {
        disconnect(m_consumers.take(consumer));
    }
    if (active() != wasActive) {
        Q_EMIT activeChanged();
        reconfigure();
    }
}
void SystemStats::setPaused(bool value)
{
    if (m_paused == value)
        return;
    m_paused = value;
    Q_EMIT pausedChanged();
    reconfigure();
}
void SystemStats::setInterval(int value)
{
    const int bounded = std::clamp(value, 1000, 5000);
    if (m_interval == bounded)
        return;
    m_interval = bounded;
    Q_EMIT intervalChanged();
    reconfigure();
}
void SystemStats::reconfigure()
{
    ++m_generation;
    m_resetRates = true;
    m_timer.stop();
    if (active() && !m_paused) {
        m_timer.start(m_interval);
        requestSample();
    }
}
void SystemStats::requestSample()
{
    if (m_busy || m_paused || !active())
        return;
    m_busy = true;
    const auto generation = m_generation;
    const bool reset = std::exchange(m_resetRates, false);
    QMetaObject::invokeMethod(
        m_worker,
        [this, generation, reset] {
            if (!m_reader)
                m_reader = std::make_unique<SystemStatsReader>();
            if (reset)
                m_reader->resetRates();
            const auto value = m_reader->sample();
            QMetaObject::invokeMethod(
                this,
                [this, generation, value] {
                    m_busy = false;
                    if (generation != m_generation) {
                        requestSample();
                        return;
                    }
                    if (active() && !m_paused)
                        acceptSample(value);
                },
                Qt::QueuedConnection);
        },
        Qt::QueuedConnection);
}
void SystemStats::acceptSample(const QVariantMap& value)
{
    if (m_snapshot != value) {
        m_snapshot = value;
        Q_EMIT changed();
    }
    auto field = [&value](const char* group, const char* key) {
        return value.value(QString::fromLatin1(group)).toMap().value(QString::fromLatin1(key), -1.0);
    };
    QVariantMap metrics{{QStringLiteral("cpu"), field("cpu", "usage")},
                        {QStringLiteral("memory"), field("memory", "used")},
                        {QStringLiteral("network"), field("network", "down")},
                        {QStringLiteral("upload"), field("network", "up")}};
    for (const auto& v : value.value(QStringLiteral("gpus")).toList()) {
        const auto gpu = v.toMap();
        metrics[QStringLiteral("gpu:") + gpu.value(QStringLiteral("id")).toString()] =
            gpu.value(QStringLiteral("usage"), -1.0);
    }
    const auto drives = value.value(QStringLiteral("storage")).toMap().value(QStringLiteral("drives")).toList();
    double storage = -1;
    for (const auto& v : drives) {
        const auto drive = v.toMap();
        if (drive.value(QStringLiteral("mount")).toString() == QLatin1String("/"))
            storage = drive.value(QStringLiteral("usage"), -1.0).toDouble();
    }
    metrics[QStringLiteral("storage")] = storage;
    const auto now = m_clock.elapsed();
    m_history.append({now, metrics});
    while (!m_history.isEmpty() && (m_history.first().time < now - 900000 || m_history.size() > 1000))
        m_history.removeFirst();
    m_revision = m_revision == std::numeric_limits<int>::max() ? 0 : m_revision + 1;
    Q_EMIT sampled();
}
QVariantList SystemStats::history(const QString& metric, int minutes) const
{
    QVariantList result;
    if (m_history.isEmpty())
        return result;
    const qint64 span = std::clamp(minutes, 1, 15) * 60000;
    const auto end = m_history.last().time, start = end - span;
    // Bound plot complexity even at a one-second refresh. Keep min/max pairs
    // per time bucket so short spikes survive a fifteen-minute view.
    const qint64 bucketWidth = std::max<qint64>(1000, span / 90);
    qint64 previousTime = -1, bucket = -1;
    QList<QPair<qint64, double>> pending;
    auto append = [&](qint64 time, double value) {
        result.append(QVariantMap{{QStringLiteral("x"), std::clamp(double(time - start) / span, 0.0, 1.0)},
                                  {QStringLiteral("value"), value}});
    };
    auto flush = [&] {
        if (pending.isEmpty())
            return;
        auto low = std::min_element(pending.cbegin(), pending.cend(), [](const auto& a, const auto& b) {
            return a.second < b.second;
        });
        auto high = std::max_element(pending.cbegin(), pending.cend(), [](const auto& a, const auto& b) {
            return a.second < b.second;
        });
        if (low->first <= high->first) {
            append(low->first, low->second);
            if (low != high)
                append(high->first, high->second);
        } else {
            append(high->first, high->second);
            append(low->first, low->second);
        }
        pending.clear();
    };
    for (const auto& frame : m_history) {
        if (frame.time < start)
            continue;
        const auto value = frame.values.value(metric, -1.0).toDouble();
        const auto nextBucket = (frame.time - start) / bucketWidth;
        if (nextBucket != bucket || value < 0 || (previousTime >= 0 && frame.time - previousTime > 10000))
            flush();
        if (previousTime >= 0 && frame.time - previousTime > 10000)
            append(frame.time - 1, -1);
        if (value < 0)
            append(frame.time, -1);
        else
            pending.append({frame.time, value});
        bucket = nextBucket;
        previousTime = frame.time;
    }
    flush();
    if (!result.isEmpty() && result.last().toMap().value(QStringLiteral("x")).toDouble() < 1)
        append(end, m_history.last().values.value(metric, -1.0).toDouble());
    return result;
}
}
