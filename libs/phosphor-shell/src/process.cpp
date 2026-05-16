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
    connect(m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) {
            onProcessFinished(-1, QProcess::CrashExit);
        }
    });
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
    // If we're transitioning to "no polling" (interval=0), explicitly
    // stop any armed timer. Otherwise an already-scheduled fire from
    // the previous interval still triggers one more run after the
    // user told us they're done with polling.
    if (m_interval <= 0) {
        m_timer->stop();
    }
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

    // Mark stdout/stderr buffers as "fresh" — the first readyRead* of
    // the new run will REPLACE the buffer rather than clear-then-emit-
    // then-append. Without this, an interval=N consumer (e.g. clock)
    // sees stdoutText briefly become empty between startProcess and
    // the first readyRead, producing a visible blink.
    m_stdoutFresh = true;
    m_stderrFresh = true;
    // Reset the stateful decoders, otherwise a partial UTF-8 sequence
    // left over from the previous run would corrupt the first character
    // of the new run's output.
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
    // Reset the stateful UTF-8 decoders here too: state-after-stop must
    // be clean so any path that mutates m_command without going through
    // startProcess (or that calls startProcess directly) doesn't drag
    // a partial-codepoint remnant from the previous run into the next.
    m_stdoutDecoder.resetState();
    m_stderrDecoder.resetState();
}

void Process::onReadyReadStdout()
{
    // First readyRead of a new run REPLACES the buffer (clears the
    // previous invocation's output atomically); subsequent readyReads
    // APPEND because readyReadStandardOutput can fire multiple times
    // per invocation. The replace-on-first-read pattern is what makes
    // interval-N consumers (clock, etc.) transition cleanly from old
    // output to new without a visible empty-text blink in between.
    // trimToCap keeps memory bounded for chatty long-running children.
    // The QStringDecoder retains any trailing partial UTF-8 sequence so
    // a codepoint straddling two chunk boundaries decodes correctly
    // when the next chunk arrives.
    const QString chunk = m_stdoutDecoder.decode(m_process->readAllStandardOutput());
    if (m_stdoutFresh) {
        m_stdout = chunk;
        m_stdoutFresh = false;
    } else {
        m_stdout.append(chunk);
    }
    trimToCap(m_stdout);
    Q_EMIT stdoutTextChanged();
}

void Process::onReadyReadStderr()
{
    const QString chunk = m_stderrDecoder.decode(m_process->readAllStandardError());
    if (m_stderrFresh) {
        m_stderr = chunk;
        m_stderrFresh = false;
    } else {
        m_stderr.append(chunk);
    }
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
    // readyRead and finished. Honour the fresh-run latch so the first
    // drained chunk REPLACES the buffer (in case readyRead never fired
    // because the child wrote everything in one shot during exit).
    // The stateful decoders consume any partial sequence the previous
    // chunk left behind and emit the proper codepoint here at
    // end-of-stream.
    const QByteArray remainingOut = m_process->readAllStandardOutput();
    if (!remainingOut.isEmpty()) {
        const QString chunk = m_stdoutDecoder.decode(remainingOut);
        if (m_stdoutFresh) {
            m_stdout = chunk;
            m_stdoutFresh = false;
        } else {
            m_stdout.append(chunk);
        }
        trimToCap(m_stdout);
        Q_EMIT stdoutTextChanged();
    } else if (m_stdoutFresh && !m_stdout.isEmpty()) {
        // Run finished without producing any output — explicitly clear
        // the buffer so stale data from the previous invocation doesn't
        // linger when the consumer next reads.
        m_stdout.clear();
        m_stdoutFresh = false;
        Q_EMIT stdoutTextChanged();
    }
    const QByteArray remainingErr = m_process->readAllStandardError();
    if (!remainingErr.isEmpty()) {
        const QString chunk = m_stderrDecoder.decode(remainingErr);
        if (m_stderrFresh) {
            m_stderr = chunk;
            m_stderrFresh = false;
        } else {
            m_stderr.append(chunk);
        }
        trimToCap(m_stderr);
        Q_EMIT stderrTextChanged();
    } else if (m_stderrFresh && !m_stderr.isEmpty()) {
        m_stderr.clear();
        m_stderrFresh = false;
        Q_EMIT stderrTextChanged();
    }

    Q_EMIT finished(exitCode, exitStatus);

    if (m_running && m_interval > 0) {
        m_timer->start(m_interval);
    }
}

} // namespace PhosphorShell
