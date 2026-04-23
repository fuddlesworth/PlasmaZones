// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorAudio/IAudioSpectrumProvider.h>

#include <QByteArray>
#include <QProcess>
#include <QString>
#include <QVector>

namespace PhosphorAudio {

class PHOSPHORAUDIO_EXPORT CavaSpectrumProvider : public IAudioSpectrumProvider
{
    Q_OBJECT

public:
    explicit CavaSpectrumProvider(QObject* parent = nullptr);
    ~CavaSpectrumProvider() override;

    static bool isCavaInstalled();
    static QString detectAudioMethod();

    bool isAvailable() const override;
    void start() override;
    void stop() override;
    bool isRunning() const override;

    int barCount() const override;
    void setBarCount(int count) override;

    int framerate() const override;
    void setFramerate(int fps) override;

    QVector<float> spectrum() const override;

private:
    void buildConfig();
    void onReadyReadStandardOutput();
    void onProcessStateChanged(QProcess::ProcessState state);
    void onProcessFinished(int exitCode, QProcess::ExitStatus exitStatus);
    void onProcessError(QProcess::ProcessError error);
    void restartAsync();

    static constexpr qsizetype kMaxStdoutBufferSize = 65536;

    QProcess* m_process = nullptr;
    QByteArray m_stdoutBuffer;
    int m_barCount = 64;
    int m_framerate = 60;
    QString m_config;
    QVector<float> m_spectrum;
    QVector<float> m_smoothedSpectrum;
    bool m_stopping = false;
    bool m_pendingRestart = false;
};

} // namespace PhosphorAudio
