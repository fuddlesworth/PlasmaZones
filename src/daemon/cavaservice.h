// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QObject>
#include <QProcess>
#include <QVector>
#include <QString>

namespace PlasmaZones {

/**
 * @brief Service that spawns CAVA and provides audio spectrum data
 *
 * Spawns CAVA as a subprocess with config on stdin (Kurve-style), reads
 * raw ASCII bar data from stdout, and emits spectrumUpdated with normalized
 * bar values (0.0-1.0).
 *
 * Requires CAVA to be installed. Uses method=raw, data_format=ascii.
 * Auto-detects PipeWire vs PulseAudio for the input method.
 */
class CavaService : public QObject
{
    Q_OBJECT

public:
    explicit CavaService(QObject* parent = nullptr);
    ~CavaService() override;

    /** Check whether the cava binary is reachable in $PATH. */
    static bool isAvailable();

    /** Start CAVA subprocess (async). Idempotent if already running. */
    void start();
    /** Stop CAVA subprocess gracefully (SIGTERM then SIGKILL fallback). */
    void stop();
    bool isRunning() const;

    /** Bar count (16-256). Must match CAVA config. */
    int barCount() const
    {
        return m_barCount;
    }
    void setBarCount(int count);

    /** Target framerate for CAVA (30-144). */
    int framerate() const
    {
        return m_framerate;
    }
    void setFramerate(int fps);

    /** Last received spectrum (0.0-1.0 per bar). Empty when not running. */
    QVector<float> spectrum() const;

Q_SIGNALS:
    void spectrumUpdated(const QVector<float>& spectrum);
    void runningChanged(bool running);
    void errorOccurred(const QString& message);

private:
    void buildConfig();
    void onReadyReadStandardOutput();
    void onProcessStateChanged(QProcess::ProcessState state);
    void onProcessError(QProcess::ProcessError error);
    void restartAsync();
    static QString detectAudioMethod();

    static constexpr qsizetype kMaxStdoutBufferSize = 65536; // 64 KB

    QProcess* m_process = nullptr;
    QByteArray m_stdoutBuffer;
    int m_barCount = 64;
    int m_framerate = 60;
    QString m_config;
    QVector<float> m_spectrum;
    bool m_stopping = false; // suppress error reporting during intentional stop
    bool m_pendingRestart = false; // suppress error reporting during async restart
};

} // namespace PlasmaZones
