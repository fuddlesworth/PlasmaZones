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
    // chunk.
    m_stdout.append(QString::fromUtf8(m_process->readAllStandardOutput()));
    Q_EMIT stdoutTextChanged();
}

void Process::onReadyReadStderr()
{
    m_stderr.append(QString::fromUtf8(m_process->readAllStandardError()));
    Q_EMIT stderrTextChanged();
}

void Process::onProcessFinished(int exitCode)
{
    m_exitCode = exitCode;
    Q_EMIT exitCodeChanged();

    // Drain any final stdout/stderr that arrived between the last
    // readyRead and finished.
    const QByteArray remainingOut = m_process->readAllStandardOutput();
    if (!remainingOut.isEmpty()) {
        m_stdout.append(QString::fromUtf8(remainingOut));
        Q_EMIT stdoutTextChanged();
    }
    const QByteArray remainingErr = m_process->readAllStandardError();
    if (!remainingErr.isEmpty()) {
        m_stderr.append(QString::fromUtf8(remainingErr));
        Q_EMIT stderrTextChanged();
    }

    Q_EMIT finished(exitCode);

    if (m_running && m_interval > 0) {
        m_timer->start(m_interval);
    }
}

} // namespace PhosphorShell
