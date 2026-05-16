// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorAudio/phosphoraudio_export.h>

#include <QObject>
#include <QVector>

namespace PhosphorAudio {

class PHOSPHORAUDIO_EXPORT IAudioSpectrumProvider : public QObject
{
    Q_OBJECT

public:
    explicit IAudioSpectrumProvider(QObject* parent = nullptr)
        : QObject(parent)
    {
    }

    ~IAudioSpectrumProvider() override = default;

    virtual bool isAvailable() const = 0;
    virtual void start() = 0;
    virtual void stop() = 0;
    virtual bool isRunning() const = 0;

    virtual int barCount() const = 0;
    virtual void setBarCount(int count) = 0;

    virtual int framerate() const = 0;
    virtual void setFramerate(int fps) = 0;

    virtual QVector<float> spectrum() const = 0;

Q_SIGNALS:
    void spectrumUpdated(const QVector<float>& spectrum);
    void runningChanged(bool running);
    void errorOccurred(const QString& message);
};

} // namespace PhosphorAudio
