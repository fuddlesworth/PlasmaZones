// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorShell/Process.h>

#include <QProcess>
#include <QTimer>

namespace PhosphorShell {

namespace {
// SIGTERM grace period before SIGKILL on shutdown — gives the child a
// chance to flush and clean up tempfiles.
constexpr int kTerminateGraceMs = 200;
constexpr int kKillTimeoutMs = 100;
// Cap on accumulated stdout/stderr buffer size. A long-running interval=0
// child with chatty output would otherwise grow the QString without
// bound; for the panel-style consumers this exposes (tail-style reads),
// keeping the most-recent ~1 MiB is plenty and bounds memory.
constexpr int kMaxBufferChars = 1024 * 1024;
// When the cap is exceeded, drop the oldest 1/4 so we don't trim on
// every chunk arrival.
constexpr int kBufferTrimChars = kMaxBufferChars / 4;

void trimToCap(QString& buf)
{
    if (buf.size() <= kMaxBufferChars) {
        return;
    }
    int cut = buf.size() - kBufferTrimChars;
    // Surrogate-pair safety: kBufferTrimChars is a fixed UTF-16 code-unit
    // count, so the boundary can land mid-surrogate. If the kept tail
    // would start on a low surrogate, advance one unit so the resulting
    // string begins at a clean code point.
    if (cut < buf.size() && buf.at(cut).isLowSurrogate()) {
        ++cut;
    }
    buf.remove(0, cut);
}
} // namespace

Process::Process(QObject* parent)
    : QObject(parent)
    , m_process(new QProcess(this))
    , m_timer(new QTimer(this))
{
    m_timer->setSingleShot(true);

    connect(m_process, &QProcess::readyReadStandardOutput, this, &Process::onReadyReadStdout);
    connect(m_process, &QProcess::readyReadStandardError, this, &Process::onReadyReadStderr);
    connect(m_process, &QProcess::finished, this, &Process::onProcessFinished);
    connect(m_timer, &QTimer::timeout, this, &Process::startProcess);
}

Process::~Process()
{
    stopProcess();
}

QStringList Process::command() const
{
    return m_command;
}

void Process::setCommand(const QStringList& command)
{
    if (m_command == command) {
        return;
    }
    m_command = command;
    Q_EMIT commandChanged();

    if (m_running) {
        stopProcess();
        startProcess();
    }
}

bool Process::running() const
{
    return m_running;
}

void Process::setRunning(bool running)
{
    if (m_running == running) {
        return;
    }
    m_running = running;

    if (running) {
        startProcess();
    } else {
        stopProcess();
    }

    Q_EMIT runningChanged();
}

int Process::interval() const
{
    return m_interval;
}

void Process::setInterval(int interval)
{
    if (m_interval == interval) {
        return;
    }
    m_interval = interval;
    Q_EMIT intervalChanged();
}

QString Process::stdoutText() const
{
    return m_stdout;
}

QString Process::stderrText() const
{
    return m_stderr;
}

int Process::exitCode() const
{
    return m_exitCode;
}

QProcess::ExitStatus Process::exitStatus() const
{
    return m_exitStatus;
}

void Process::startProcess()
{
    if (m_command.isEmpty()) {
        return;
    }

    // Reset accumulated output for the new run so consumers see only this
    // invocation's output, not stale bytes from the previous run.
    if (!m_stdout.isEmpty()) {
        m_stdout.clear();
        Q_EMIT stdoutTextChanged();
    }
    if (!m_stderr.isEmpty()) {
        m_stderr.clear();
        Q_EMIT stderrTextChanged();
    }
    // Reset the stateful decoders too, otherwise a partial UTF-8
    // sequence left over from the previous run would corrupt the
    // first character of the new run's output.
    m_stdoutDecoder.resetState();
    m_stderrDecoder.resetState();

    const QString program = m_command.first();
    const QStringList args = m_command.mid(1);
    m_process->start(program, args);
}

void Process::stopProcess()
{
    m_timer->stop();
    if (m_process->state() != QProcess::NotRunning) {
        // Try graceful SIGTERM first so the child can release tempfiles,
        // sockets, and any other resources before being force-killed.
        m_process->terminate();
        if (!m_process->waitForFinished(kTerminateGraceMs)) {
            m_process->kill();
            m_process->waitForFinished(kKillTimeoutMs);
        }
    }
}

void Process::onReadyReadStdout()
{
    // Append, don't replace — readyReadStandardOutput can fire multiple
    // times per invocation, and replacing would lose all but the last
    // chunk. trimToCap keeps memory bounded for chatty long-running
    // children (interval=0 stream subscriptions). The QStringDecoder
    // retains any trailing partial UTF-8 sequence so a codepoint
    // straddling two chunk boundaries decodes correctly when the next
    // chunk arrives — using QString::fromUtf8 per chunk would emit
    // U+FFFD on both halves.
    m_stdout.append(m_stdoutDecoder.decode(m_process->readAllStandardOutput()));
    trimToCap(m_stdout);
    Q_EMIT stdoutTextChanged();
}

void Process::onReadyReadStderr()
{
    m_stderr.append(m_stderrDecoder.decode(m_process->readAllStandardError()));
    trimToCap(m_stderr);
    Q_EMIT stderrTextChanged();
}

void Process::onProcessFinished(int exitCode, QProcess::ExitStatus exitStatus)
{
    if (m_exitCode != exitCode) {
        m_exitCode = exitCode;
        Q_EMIT exitCodeChanged();
    }
    if (m_exitStatus != exitStatus) {
        m_exitStatus = exitStatus;
        Q_EMIT exitStatusChanged();
    }

    // Drain any final stdout/stderr that arrived between the last
    // readyRead and finished. Use the same stateful decoders so any
    // partial sequence the previous chunk left behind is consumed and
    // emitted as the proper codepoint here at end-of-stream.
    const QByteArray remainingOut = m_process->readAllStandardOutput();
    if (!remainingOut.isEmpty()) {
        m_stdout.append(m_stdoutDecoder.decode(remainingOut));
        trimToCap(m_stdout);
        Q_EMIT stdoutTextChanged();
    }
    const QByteArray remainingErr = m_process->readAllStandardError();
    if (!remainingErr.isEmpty()) {
        m_stderr.append(m_stderrDecoder.decode(remainingErr));
        trimToCap(m_stderr);
        Q_EMIT stderrTextChanged();
    }

    Q_EMIT finished(exitCode, exitStatus);

    if (m_running && m_interval > 0) {
        m_timer->start(m_interval);
    }
}

} // namespace PhosphorShell
