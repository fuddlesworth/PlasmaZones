// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once
#include <QObject>
#include <QSet>
#include <QVariantList>
#include <QtQmlIntegration/qqmlintegration.h>
namespace PhosphorAudio {
class IAudioSpectrumProvider;
}

// One analyzer per QML engine, shared by every visible media surface.
class AudioSpectrum : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON
    Q_PROPERTY(QVariantList samples READ samples NOTIFY samplesChanged)
    Q_PROPERTY(bool available READ available CONSTANT)
    Q_PROPERTY(int consumers READ consumers NOTIFY consumersChanged)
public:
    explicit AudioSpectrum(QObject* parent = nullptr);
    explicit AudioSpectrum(PhosphorAudio::IAudioSpectrumProvider* provider, QObject* parent);
    QVariantList samples() const
    {
        return m_samples;
    }
    bool available() const;
    int consumers() const
    {
        return m_consumers.size();
    }
    Q_INVOKABLE void setActive(QObject* consumer, bool active);
Q_SIGNALS:
    void samplesChanged();
    void consumersChanged();

private:
    PhosphorAudio::IAudioSpectrumProvider* m_provider;
    QSet<QObject*> m_consumers;
    QSet<QObject*> m_watched;
    QVariantList m_samples;
};
