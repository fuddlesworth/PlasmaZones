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
    /// unknown input methods coerced back to auto-detect (empty). Public so
    /// callers can preview the applied values before committing a set.
    static SpectrumOptions normalizedOptions(SpectrumOptions options);

    /// The cava config-file text the provider would run @p options with
    /// (normalized first, so hostile or out-of-range values are emitted in
    /// their sanitized form). This is what start() writes to cava's stdin;
    /// exposed so the generated config is testable without spawning cava.
    static QString generateConfig(const SpectrumOptions& options);

    bool isAvailable() const override;
    void start() override;
    void stop() override;
    bool isRunning() const override;

    SpectrumOptions options() const override;
    void setOptions(const SpectrumOptions& options) override;

    QVector<float> spectrum() const override;

private:
    void onReadyReadStandardOutput();
    void onProcessStateChanged(QProcess::ProcessState state);
    void onProcessFinished(int exitCode, QProcess::ExitStatus exitStatus);
    void onProcessError(QProcess::ProcessError error);
    void restartAsync();

    static constexpr qsizetype kMaxStdoutBufferSize = 65536;

    QProcess* m_process = nullptr;
    QByteArray m_stdoutBuffer;
    SpectrumOptions m_options;
    QVector<float> m_spectrum;
    QVector<float> m_smoothedSpectrum;
    bool m_stopping = false;
    bool m_pendingRestart = false;
    // Incremented each time restartAsync() arms a kill-escalation timer, so a
    // timer left over from an earlier restart cannot cut a later restart's
    // grace window short (m_process is one reused object across restarts, so
    // the timer cannot tell generations apart by pointer).
    int m_restartEpoch = 0;
};

} // namespace PhosphorAudio
