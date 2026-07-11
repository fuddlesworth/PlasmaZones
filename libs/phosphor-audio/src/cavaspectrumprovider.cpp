// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorAudio/CavaSpectrumProvider.h>
#include <PhosphorAudio/AudioDefaults.h>

#include <QDir>
#include <QFileInfo>
#include <QLoggingCategory>
#include <QStandardPaths>

namespace PhosphorAudio {

Q_LOGGING_CATEGORY(lcPhosphorAudio, "phosphor.audio")

static constexpr int kAsciiMaxRange = 1000;
static_assert(Defaults::MinBars % 2 == 0, "MinBars must be even for CAVA stereo");
static_assert(Defaults::MaxBars % 2 == 0, "MaxBars must be even for CAVA stereo");
static_assert(Defaults::MaxLowerCutoffHz < Defaults::MinHigherCutoffHz,
              "cutoff ranges must not overlap: cava rejects lower_cutoff_freq >= higher_cutoff_freq");

QString channelModeToString(ChannelMode mode)
{
    switch (mode) {
    case ChannelMode::MonoAverage:
        return QStringLiteral("mono-average");
    case ChannelMode::MonoLeft:
        return QStringLiteral("mono-left");
    case ChannelMode::MonoRight:
        return QStringLiteral("mono-right");
    case ChannelMode::Stereo:
        break;
    }
    return QStringLiteral("stereo");
}

ChannelMode channelModeFromString(const QString& mode)
{
    if (mode == QLatin1String("mono-average")) {
        return ChannelMode::MonoAverage;
    }
    if (mode == QLatin1String("mono-left")) {
        return ChannelMode::MonoLeft;
    }
    if (mode == QLatin1String("mono-right")) {
        return ChannelMode::MonoRight;
    }
    return ChannelMode::Stereo;
}

namespace {

// The option strings land on their own `key=value` lines in the generated
// config, so strip anything that could break out of the line (or confuse
// cava's INI parser) and trim to the meaningful text.
QString sanitizedConfigValue(const QString& raw)
{
    QString out;
    out.reserve(raw.size());
    for (const QChar c : raw) {
        if (!c.isPrint()) {
            continue;
        }
        out.append(c);
    }
    return out.trimmed();
}

bool isKnownInputMethod(const QString& method)
{
    // The capture backends upstream cava can be built with. The settings UI
    // only offers auto/pipewire/pulse; the rest stay reachable for power
    // users editing config.json directly.
    static const QStringList kMethods = {
        QStringLiteral("pipewire"),  QStringLiteral("pulse"), QStringLiteral("alsa"),
        QStringLiteral("jack"),      QStringLiteral("sndio"), QStringLiteral("oss"),
        QStringLiteral("portaudio"), QStringLiteral("fifo"),  QStringLiteral("shmem"),
    };
    return kMethods.contains(method);
}

} // namespace

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

SpectrumOptions CavaSpectrumProvider::normalizedOptions(SpectrumOptions options)
{
    const int evenBars = (options.barCount % 2 != 0) ? options.barCount + 1 : options.barCount;
    options.barCount = qBound(Defaults::MinBars, evenBars, Defaults::MaxBars);
    options.framerate = qBound(Defaults::MinFramerate, options.framerate, Defaults::MaxFramerate);
    options.sensitivity = qBound(Defaults::MinSensitivity, options.sensitivity, Defaults::MaxSensitivity);
    options.noiseReduction = qBound(Defaults::MinNoiseReduction, options.noiseReduction, Defaults::MaxNoiseReduction);
    options.lowerCutoffHz = qBound(Defaults::MinLowerCutoffHz, options.lowerCutoffHz, Defaults::MaxLowerCutoffHz);
    options.higherCutoffHz = qBound(Defaults::MinHigherCutoffHz, options.higherCutoffHz, Defaults::MaxHigherCutoffHz);
    options.extraSmoothing = qBound(Defaults::MinExtraSmoothing, options.extraSmoothing, Defaults::MaxExtraSmoothing);

    options.inputMethod = sanitizedConfigValue(options.inputMethod).toLower();
    if (!options.inputMethod.isEmpty() && !isKnownInputMethod(options.inputMethod)) {
        qCWarning(lcPhosphorAudio) << "Unknown CAVA input method" << options.inputMethod << "- using auto-detection";
        options.inputMethod.clear();
    }
    options.inputSource = sanitizedConfigValue(options.inputSource);
    if (options.inputSource.isEmpty()) {
        options.inputSource = QStringLiteral("auto");
    }
    return options;
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
    // Feed the generated config through stdin (cava reads it as the file
    // /dev/stdin). A direct spawn — no shell in between — so option strings
    // can never be interpreted by anything but cava's own INI parser.
    m_process->start(cavaPath, QStringList{QStringLiteral("-p"), QStringLiteral("/dev/stdin")});
    m_process->write(m_config.toUtf8());
    m_process->closeWriteChannel();
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

SpectrumOptions CavaSpectrumProvider::options() const
{
    return m_options;
}

void CavaSpectrumProvider::setOptions(const SpectrumOptions& options)
{
    const SpectrumOptions normalized = normalizedOptions(options);
    if (m_options == normalized) {
        return;
    }
    m_options = normalized;
    if (isRunning()) {
        restartAsync();
    }
}

QVector<float> CavaSpectrumProvider::spectrum() const
{
    return m_spectrum;
}

void CavaSpectrumProvider::buildConfig()
{
    const QString audioMethod = m_options.inputMethod.isEmpty() ? detectAudioMethod() : m_options.inputMethod;
    const bool mono = m_options.channelMode != ChannelMode::Stereo;
    QString monoOption = QStringLiteral("average");
    if (m_options.channelMode == ChannelMode::MonoLeft) {
        monoOption = QStringLiteral("left");
    } else if (m_options.channelMode == ChannelMode::MonoRight) {
        monoOption = QStringLiteral("right");
    }

    m_config = QStringLiteral(
                   "[general]\n"
                   "framerate=%1\n"
                   "bars=%2\n"
                   "autosens=%3\n"
                   "sensitivity=%4\n"
                   "lower_cutoff_freq=%5\n"
                   "higher_cutoff_freq=%6\n")
                   .arg(m_options.framerate)
                   .arg(m_options.barCount)
                   .arg(m_options.autosens ? 1 : 0)
                   .arg(m_options.sensitivity)
                   .arg(m_options.lowerCutoffHz)
                   .arg(m_options.higherCutoffHz);
    m_config += QStringLiteral(
                    "[input]\n"
                    "method=%1\n"
                    "source=%2\n")
                    .arg(audioMethod, m_options.inputSource);
    m_config += QStringLiteral(
                    "[output]\n"
                    "method=raw\n"
                    "raw_target=/dev/stdout\n"
                    "data_format=ascii\n"
                    "ascii_max_range=%1\n"
                    "bar_delimiter=59\n"
                    "frame_delimiter=10\n"
                    "channels=%2\n"
                    "mono_option=%3\n"
                    "reverse=%4\n")
                    .arg(kAsciiMaxRange)
                    .arg(mono ? QStringLiteral("mono") : QStringLiteral("stereo"), monoOption)
                    .arg(m_options.reverse ? 1 : 0);
    m_config += QStringLiteral(
                    "[smoothing]\n"
                    "noise_reduction=%1\n"
                    "monstercat=%2\n"
                    "waves=%3\n")
                    .arg(m_options.noiseReduction)
                    .arg(m_options.monstercat ? 1 : 0)
                    .arg(m_options.waves ? 1 : 0);
}

void CavaSpectrumProvider::onReadyReadStandardOutput()
{
    if (!m_process) {
        return;
    }
    m_stdoutBuffer += m_process->readAllStandardOutput();

    if (m_stdoutBuffer.size() > kMaxStdoutBufferSize) {
        qCWarning(lcPhosphorAudio) << "CAVA stdout buffer exceeded" << kMaxStdoutBufferSize
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
            const float retain = static_cast<float>(m_options.extraSmoothing);
            if (retain > 0.0f && m_smoothedSpectrum.size() == spectrum.size()) {
                for (int i = 0; i < spectrum.size(); ++i) {
                    m_smoothedSpectrum[i] = (1.0f - retain) * spectrum[i] + retain * m_smoothedSpectrum[i];
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
    if (state == QProcess::Starting) {
        return;
    }
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
        const QString msg = QStringLiteral("CAVA exited with code %1").arg(exitCode);
        qCWarning(lcPhosphorAudio) << msg << "stderr:" << stderrOutput;
        Q_EMIT errorOccurred(msg);
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
    qCWarning(lcPhosphorAudio) << "CAVA process error:" << error << msg;
    Q_EMIT errorOccurred(msg);
}

void CavaSpectrumProvider::restartAsync()
{
    if (m_pendingRestart) {
        // A restart is already queued; the queued start() runs buildConfig()
        // and picks up whatever m_options holds by then.
        return;
    }
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
