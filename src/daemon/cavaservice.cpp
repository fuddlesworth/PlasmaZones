// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "cavaservice.h"

#include <QDir>
#include <QStandardPaths>
#include <QFileInfo>

#include "../core/logging.h"

namespace PlasmaZones {

static constexpr int kAsciiMaxRange = 1000; // CAVA ascii_max_range
static constexpr int kMinBars = 16;
static constexpr int kMaxBars = 256;

CavaService::CavaService(QObject* parent)
    : QObject(parent)
{
}

CavaService::~CavaService()
{
    stop();
}

bool CavaService::isAvailable()
{
    return !QStandardPaths::findExecutable(QStringLiteral("cava")).isEmpty();
}

void CavaService::start()
{
    if (m_process && m_process->state() != QProcess::NotRunning) {
        return;
    }

    // Find cava binary
    const QString cavaPath = QStandardPaths::findExecutable(QStringLiteral("cava"));
    if (cavaPath.isEmpty()) {
        qCWarning(lcOverlay) << "CAVA not found in PATH. Install cava for audio visualization.";
        Q_EMIT errorOccurred(QStringLiteral("CAVA not found. Install cava for audio visualization."));
        return;
    }

    buildConfig();

    if (!m_process) {
        m_process = new QProcess(this);
        connect(m_process, &QProcess::readyReadStandardOutput,
                this, &CavaService::onReadyReadStandardOutput);
        connect(m_process, &QProcess::stateChanged,
                this, &CavaService::onProcessStateChanged);
        connect(m_process, &QProcess::errorOccurred,
                this, &CavaService::onProcessError);
    }

    m_stdoutBuffer.clear();
    m_spectrum.clear();

    // Kurve-style: pass config via stdin, read raw output from stdout
    m_process->setProcessChannelMode(QProcess::ForwardedErrorChannel);
    m_process->start(QStringLiteral("sh"), QStringList{QStringLiteral("-c"),
        QStringLiteral("exec %1 -p /dev/stdin <<'CAVAEOF'\n%2\nCAVAEOF").arg(cavaPath, m_config)});

    // Async start: errors reported via QProcess::errorOccurred signal (already connected).
    // No blocking waitForStarted() to avoid freezing the GUI thread.
}

void CavaService::stop()
{
    if (m_process && m_process->state() != QProcess::NotRunning) {
        m_stopping = true;
        // Graceful: SIGTERM first, then SIGKILL if unresponsive
        m_process->terminate();
        if (!m_process->waitForFinished(500)) {
            m_process->kill();
            m_process->waitForFinished(500);
        }
        m_stopping = false;
    }
    m_spectrum.clear();
}

bool CavaService::isRunning() const
{
    return m_process && m_process->state() == QProcess::Running;
}

void CavaService::setBarCount(int count)
{
    const int clamped = qBound(kMinBars, count, kMaxBars);
    if (m_barCount != clamped) {
        m_barCount = clamped;
        if (isRunning()) {
            restartAsync();
        }
    }
}

void CavaService::setFramerate(int fps)
{
    const int clamped = qBound(30, fps, 144);
    if (m_framerate != clamped) {
        m_framerate = clamped;
        if (isRunning()) {
            restartAsync();
        }
    }
}

QVector<float> CavaService::spectrum() const
{
    return m_spectrum;
}

QString CavaService::detectAudioMethod()
{
    // Prefer PipeWire (Plasma 6 standard), fall back to PulseAudio
    const QString runtimeDir = QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation);
    if (!runtimeDir.isEmpty() && QFileInfo::exists(runtimeDir + QStringLiteral("/pipewire-0"))) {
        return QStringLiteral("pipewire");
    }
    if (!QStandardPaths::findExecutable(QStringLiteral("pw-cli")).isEmpty()) {
        return QStringLiteral("pipewire");
    }
    return QStringLiteral("pulse");
}

void CavaService::buildConfig()
{
    const QString audioMethod = detectAudioMethod();
    // CAVA config: raw output, ascii format, auto-detected input
    m_config = QStringLiteral(
        "[general]\n"
        "framerate=%1\n"
        "bars=%2\n"
        "autosens=1\n"
        "lower_cutoff_freq=50\n"
        "higher_cutoff_freq=10000\n"
        "[input]\n"
        "method=%4\n"
        "source=auto\n"
        "[output]\n"
        "method=raw\n"
        "raw_target=/dev/stdout\n"
        "data_format=ascii\n"
        "ascii_max_range=%3\n"
        "bar_delimiter=59\n"
        "frame_delimiter=10\n"
        "[smoothing]\n"
        "noise_reduction=77\n"
        "monstercat=0\n"
        "waves=0\n"
    ).arg(m_framerate).arg(m_barCount).arg(kAsciiMaxRange).arg(audioMethod);
}

void CavaService::onReadyReadStandardOutput()
{
    if (!m_process) {
        return;
    }
    m_stdoutBuffer += m_process->readAllStandardOutput();

    // Guard against unbounded buffer growth from malformed data (no newlines)
    if (m_stdoutBuffer.size() > kMaxStdoutBufferSize) {
        qCWarning(lcOverlay) << "CAVA stdout buffer exceeded" << kMaxStdoutBufferSize
                             << "bytes, discarding oldest data";
        m_stdoutBuffer = m_stdoutBuffer.mid(m_stdoutBuffer.size() - kMaxStdoutBufferSize / 2);
    }

    qsizetype newlineIndex;
    while ((newlineIndex = m_stdoutBuffer.indexOf('\n')) != -1) {
        QByteArray line = m_stdoutBuffer.left(newlineIndex).trimmed();
        m_stdoutBuffer = m_stdoutBuffer.mid(newlineIndex + 1);
        if (line.isEmpty()) {
            continue;
        }
        // Remove trailing semicolon if present
        if (line.endsWith(';')) {
            line.chop(1);
        }
        const QList<QByteArray> parts = line.split(';');
        QVector<float> spectrum;
        spectrum.reserve(parts.size());
        for (const QByteArray& part : parts) {
            bool ok = false;
            const int val = part.trimmed().toInt(&ok);
            if (ok) {
                spectrum.append(qBound(0, val, kAsciiMaxRange) / static_cast<float>(kAsciiMaxRange));
            }
        }
        if (!spectrum.isEmpty()) {
            m_spectrum = std::move(spectrum);
            Q_EMIT spectrumUpdated(m_spectrum);
        }
    }
}

void CavaService::onProcessStateChanged(QProcess::ProcessState state)
{
    const bool running = (state == QProcess::Running);
    Q_EMIT runningChanged(running);
    if (!running) {
        m_spectrum.clear();
    }
}

void CavaService::onProcessError(QProcess::ProcessError error)
{
    // Suppress errors from intentional stop() or restartAsync() — SIGTERM causes QProcess::Crashed
    if (m_stopping || m_pendingRestart) {
        return;
    }
    const QString msg = m_process ? m_process->errorString() : QStringLiteral("Unknown error");
    qCWarning(lcOverlay) << "CAVA process error:" << error << msg;
    Q_EMIT errorOccurred(msg);
}

void CavaService::restartAsync()
{
    if (!m_process || m_process->state() == QProcess::NotRunning) {
        start();
        return;
    }
    m_pendingRestart = true;
    // One-shot: restart after the current process exits
    connect(m_process, &QProcess::finished, this, [this]() {
        m_pendingRestart = false;
        start();
    }, Qt::SingleShotConnection);
    m_process->terminate();
}

} // namespace PlasmaZones
