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

    /// Clamp/sanitize an option set to the values the provider would actually
    /// run with: integer ranges bounded to AudioDefaults, bar count rounded up
    /// to even, control characters stripped from the input strings, and
    /// unknown input methods coerced back to auto-detect (empty).
    static SpectrumOptions normalizedOptions(SpectrumOptions options);

    bool isAvailable() const override;
    void start() override;
    void stop() override;
    bool isRunning() const override;

    SpectrumOptions options() const override;
    void setOptions(const SpectrumOptions& options) override;

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
    SpectrumOptions m_options;
    QString m_config;
    QVector<float> m_spectrum;
    QVector<float> m_smoothedSpectrum;
    bool m_stopping = false;
    bool m_pendingRestart = false;
};

} // namespace PhosphorAudio
