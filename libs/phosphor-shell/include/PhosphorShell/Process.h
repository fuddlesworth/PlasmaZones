// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorShell/phosphorshell_export.h>

#include <QObject>
#include <QStringList>
#include <QtQml/qqmlregistration.h>

QT_BEGIN_NAMESPACE
class QProcess;
class QTimer;
QT_END_NAMESPACE

namespace PhosphorShell {

class PHOSPHORSHELL_EXPORT Process : public QObject
{
    Q_OBJECT
    QML_NAMED_ELEMENT(Process)

    Q_PROPERTY(QStringList command READ command WRITE setCommand NOTIFY commandChanged)
    Q_PROPERTY(bool running READ running WRITE setRunning NOTIFY runningChanged)
    Q_PROPERTY(int interval READ interval WRITE setInterval NOTIFY intervalChanged)
    // Property names are `stdoutText` / `stderrText` — `stdout` and `stderr`
    // are macros in <cstdio>, and any TU that transitively includes <cstdio>
    // before this header would have moc see `Q_PROPERTY(QString (*) ...)`
    // and fail to compile. The defensive rename keeps the QML name distinct
    // from the macro.
    Q_PROPERTY(QString stdoutText READ stdoutText NOTIFY stdoutTextChanged)
    Q_PROPERTY(QString stderrText READ stderrText NOTIFY stderrTextChanged)
    Q_PROPERTY(int exitCode READ exitCode NOTIFY exitCodeChanged)

public:
    explicit Process(QObject* parent = nullptr);
    ~Process() override;

    QStringList command() const;
    void setCommand(const QStringList& command);

    bool running() const;
    void setRunning(bool running);

    int interval() const;
    void setInterval(int interval);

    QString stdoutText() const;
    QString stderrText() const;
    int exitCode() const;

Q_SIGNALS:
    void commandChanged();
    void runningChanged();
    void intervalChanged();
    void stdoutTextChanged();
    void stderrTextChanged();
    void exitCodeChanged();
    void finished(int exitCode);

private Q_SLOTS:
    void onProcessFinished(int exitCode);
    void onReadyReadStdout();
    void onReadyReadStderr();

private:
    void startProcess();
    void stopProcess();

    QStringList m_command;
    bool m_running = false;
    int m_interval = 0;
    QString m_stdout;
    QString m_stderr;
    int m_exitCode = 0;
    QProcess* m_process = nullptr;
    QTimer* m_timer = nullptr;
};

} // namespace PhosphorShell
