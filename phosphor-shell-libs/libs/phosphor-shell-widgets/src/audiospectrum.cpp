// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
#include "AudioSpectrum.h"
#include <PhosphorAudio/CavaSpectrumProvider.h>
#include <algorithm>
#include <cmath>

AudioSpectrum::AudioSpectrum(QObject* parent)
    : AudioSpectrum(new PhosphorAudio::CavaSpectrumProvider, parent)
{
    m_provider->setParent(this);
}
AudioSpectrum::AudioSpectrum(PhosphorAudio::IAudioSpectrumProvider* provider, QObject* parent)
    : QObject(parent)
    , m_provider(provider)
{
    auto options = provider->options();
    options.barCount = 48;
    options.framerate = 30;
    options.channelMode = PhosphorAudio::ChannelMode::MonoAverage;
    options.extraSmoothing = 0.65;
    provider->setOptions(options);
    connect(provider, &PhosphorAudio::IAudioSpectrumProvider::spectrumUpdated, this,
            [this](const QVector<float>& values) {
                m_samples.clear();
                for (const auto value : values.mid(0, 64))
                    m_samples.append(std::isfinite(value) ? std::clamp(value, 0.0f, 1.0f) : 0.0f);
                Q_EMIT samplesChanged();
            });
}
bool AudioSpectrum::available() const
{
    return m_provider->isAvailable();
}
void AudioSpectrum::setActive(QObject* consumer, bool active)
{
    if (!consumer || active == m_consumers.contains(consumer))
        return;
    if (active) {
        if (!m_watched.contains(consumer)) {
            m_watched.insert(consumer);
            connect(consumer, &QObject::destroyed, this, [this, consumer] {
                setActive(consumer, false);
                m_watched.remove(consumer);
            });
        }
        m_consumers.insert(consumer);
        if (m_consumers.size() == 1 && available())
            m_provider->start();
    } else {
        m_consumers.remove(consumer);
        if (m_consumers.isEmpty()) {
            m_provider->stop();
            m_samples.clear();
            Q_EMIT samplesChanged();
        }
    }
    Q_EMIT consumersChanged();
}
