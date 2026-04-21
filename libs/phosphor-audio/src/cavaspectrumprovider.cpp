// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorAudio/CavaSpectrumProvider.h>
#include <PhosphorAudio/AudioDefaults.h>

#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>

namespace PhosphorAudio {

static constexpr int kAsciiMaxRange = 1000;
static_assert(Defaults::MinBars % 2 == 0, "MinBars must be even for CAVA stereo");
static_assert(Defaults::MaxBars % 2 == 0, "MaxBars must be even for CAVA stereo");

CavaSpectrumProvider::CavaSpectrumProvider(QObject* parent)
    : IAudioSpectrumProvider(parent)
{
}

CavaSpectrumProvider::~CavaSpectrumProvider()
{
    stop();
}

bool CavaSpectrumProvider::isCavaInstalled()
{
    return !QStandardPaths::findExecutable(QStringLiteral("cava")).isEmpty();
}

bool CavaSpectrumProvider::isAvailable() const
{
    return isCavaInstalled();
}

QString CavaSpectrumProvider::detectAudioMethod()
{
    const QString runtimeDir = QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation);
    if (!runtimeDir.isEmpty() && QFileInfo::exists(runtimeDir + QStringLiteral("/pipewire-0"))) {
        return QStringLiteral("pipewire");
    }
    if (!QStandardPaths::findExecutable(QStringLiteral("pw-cli")).isEmpty()) {
        return QStringLiteral("pipewire");
    }
    return QStringLiteral("pulse");
}

void CavaSpectrumProvider::start()
{
    if (m_process && m_process->state() != QProcess::NotRunning) {
        return;
    }

    const QString cavaPath = QStandardPaths::findExecutable(QStringLiteral("cava"));
    if (cavaPath.isEmpty()) {
        Q_EMIT errorOccurred(QStringLiteral("CAVA not found. Install cava for audio visualization."));
        return;
    }

    buildConfig();

    if (!m_process) {
        m_process = new QProcess(this);
        connect(m_process, &QProcess::readyReadStandardOutput, this, &CavaSpectrumProvider::onReadyReadStandardOutput);
        connect(m_process, &QProcess::stateChanged, this, &CavaSpectrumProvider::onProcessStateChanged);
        connect(m_process, &QProcess::finished, this, &CavaSpectrumProvider::onProcessFinished);
        connect(m_process, &QProcess::errorOccurred, this, &CavaSpectrumProvider::onProcessError);
    }

    m_stdoutBuffer.clear();
    m_spectrum.clear();
    m_smoothedSpectrum.clear();

    m_process->setProcessChannelMode(QProcess::SeparateChannels);
    m_process->start(
        QStringLiteral("sh"),
        QStringList{QStringLiteral("-c"),
                    QStringLiteral("exec %1 -p /dev/stdin <<'CAVAEOF'\n%2\nCAVAEOF").arg(cavaPath, m_config)});
}

void CavaSpectrumProvider::stop()
{
    if (m_process && m_process->state() != QProcess::NotRunning) {
        m_stopping = true;
        m_process->terminate();
        if (!m_process->waitForFinished(500)) {
            m_process->kill();
            m_process->waitForFinished(500);
        }
        m_stopping = false;
    }
    m_spectrum.clear();
    m_smoothedSpectrum.clear();
}

bool CavaSpectrumProvider::isRunning() const
{
    return m_process && m_process->state() == QProcess::Running;
}

int CavaSpectrumProvider::barCount() const
{
    return m_barCount;
}

void CavaSpectrumProvider::setBarCount(int count)
{
    int even = (count % 2 != 0) ? count + 1 : count;
    const int clamped = qBound(Defaults::MinBars, even, Defaults::MaxBars);
    if (m_barCount != clamped) {
        m_barCount = clamped;
        if (isRunning()) {
            restartAsync();
        }
    }
}

int CavaSpectrumProvider::framerate() const
{
    return m_framerate;
}

void CavaSpectrumProvider::setFramerate(int fps)
{
    const int clamped = qBound(Defaults::MinFramerate, fps, Defaults::MaxFramerate);
    if (m_framerate != clamped) {
        m_framerate = clamped;
        if (isRunning()) {
            restartAsync();
        }
    }
}

QVector<float> CavaSpectrumProvider::spectrum() const
{
    return m_spectrum;
}

void CavaSpectrumProvider::buildConfig()
{
    const QString audioMethod = detectAudioMethod();
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
                   "waves=0\n")
                   .arg(m_framerate)
                   .arg(m_barCount)
                   .arg(kAsciiMaxRange)
                   .arg(audioMethod);
}

void CavaSpectrumProvider::onReadyReadStandardOutput()
{
    if (!m_process) {
        return;
    }
    m_stdoutBuffer += m_process->readAllStandardOutput();

    if (m_stdoutBuffer.size() > kMaxStdoutBufferSize) {
        m_stdoutBuffer = m_stdoutBuffer.mid(m_stdoutBuffer.size() - kMaxStdoutBufferSize / 2);
    }

    qsizetype newlineIndex;
    while ((newlineIndex = m_stdoutBuffer.indexOf('\n')) != -1) {
        QByteArray line = m_stdoutBuffer.left(newlineIndex).trimmed();
        m_stdoutBuffer = m_stdoutBuffer.mid(newlineIndex + 1);
        if (line.isEmpty()) {
            continue;
        }
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
            static constexpr float kSmoothingAlpha = 0.5f;
            if (m_smoothedSpectrum.size() == spectrum.size()) {
                for (int i = 0; i < spectrum.size(); ++i) {
                    m_smoothedSpectrum[i] =
                        kSmoothingAlpha * spectrum[i] + (1.0f - kSmoothingAlpha) * m_smoothedSpectrum[i];
                }
            } else {
                m_smoothedSpectrum = spectrum;
            }
            m_spectrum = m_smoothedSpectrum;
            Q_EMIT spectrumUpdated(m_spectrum);
        }
    }
}

void CavaSpectrumProvider::onProcessStateChanged(QProcess::ProcessState state)
{
    const bool running = (state == QProcess::Running);
    Q_EMIT runningChanged(running);
    if (!running) {
        m_spectrum.clear();
    }
}

void CavaSpectrumProvider::onProcessFinished(int exitCode, QProcess::ExitStatus exitStatus)
{
    if (m_stopping || m_pendingRestart) {
        return;
    }
    if (exitStatus == QProcess::CrashExit && (exitCode == 15 || exitCode == 9)) {
        return;
    }
    if (exitCode != 0) {
        const QByteArray stderrOutput = m_process ? m_process->readAllStandardError().left(500) : QByteArray();
        Q_UNUSED(stderrOutput)
    }
}

void CavaSpectrumProvider::onProcessError(QProcess::ProcessError error)
{
    if (m_stopping || m_pendingRestart) {
        return;
    }
    if (error == QProcess::Crashed) {
        return;
    }
    const QString msg = m_process ? m_process->errorString() : QStringLiteral("Unknown error");
    Q_EMIT errorOccurred(msg);
}

void CavaSpectrumProvider::restartAsync()
{
    if (!m_process || m_process->state() == QProcess::NotRunning) {
        start();
        return;
    }
    m_pendingRestart = true;
    connect(
        m_process, &QProcess::finished, this,
        [this]() {
            m_pendingRestart = false;
            start();
        },
        Qt::SingleShotConnection);
    m_process->terminate();
}

} // namespace PhosphorAudio
