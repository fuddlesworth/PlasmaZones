// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorShell/Process.h>

#include <QProcess>
#include <QTimer>

namespace PhosphorShell {

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

QString Process::stdoutStr() const
{
    return m_stdout;
}

QString Process::stderrStr() const
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

    const QString program = m_command.first();
    const QStringList args = m_command.mid(1);
    m_process->start(program, args);
}

void Process::stopProcess()
{
    m_timer->stop();
    if (m_process->state() != QProcess::NotRunning) {
        m_process->kill();
        m_process->waitForFinished(100);
    }
}

void Process::onReadyReadStdout()
{
    m_stdout = QString::fromUtf8(m_process->readAllStandardOutput());
    Q_EMIT stdoutChanged();
}

void Process::onReadyReadStderr()
{
    m_stderr = QString::fromUtf8(m_process->readAllStandardError());
    Q_EMIT stderrChanged();
}

void Process::onProcessFinished(int exitCode)
{
    m_exitCode = exitCode;

    const QByteArray remaining = m_process->readAllStandardOutput();
    if (!remaining.isEmpty()) {
        m_stdout = QString::fromUtf8(remaining);
        Q_EMIT stdoutChanged();
    }

    Q_EMIT finished(exitCode);

    if (m_running && m_interval > 0) {
        m_timer->start(m_interval);
    }
}

} // namespace PhosphorShell
