// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorShell/phosphorshell_export.h>

#include <QObject>
#include <QProcess>
#include <QStringConverter>
#include <QStringList>
#include <QtQml/qqmlregistration.h>

QT_BEGIN_NAMESPACE
class QTimer;
QT_END_NAMESPACE

namespace PhosphorShell {

class PHOSPHORSHELL_EXPORT Process : public QObject
{
    Q_OBJECT

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
    /// Last process exit status (NormalExit / CrashExit). A child that
    /// segfaults emits exitCode=0+CrashExit on Linux; without this
    /// property consumers see exitCode=0 and assume success.
    Q_PROPERTY(QProcess::ExitStatus exitStatus READ exitStatus NOTIFY exitStatusChanged)

public:
    explicit Process(QObject* parent = nullptr);
    ~Process() override;

    [[nodiscard]] QStringList command() const;
    void setCommand(const QStringList& command);

    [[nodiscard]] bool running() const;
    void setRunning(bool running);

    [[nodiscard]] int interval() const;
    void setInterval(int interval);

    [[nodiscard]] QString stdoutText() const;
    [[nodiscard]] QString stderrText() const;
    [[nodiscard]] int exitCode() const;
    [[nodiscard]] QProcess::ExitStatus exitStatus() const;

Q_SIGNALS:
    void commandChanged();
    void runningChanged();
    void intervalChanged();
    void stdoutTextChanged();
    void stderrTextChanged();
    void exitCodeChanged();
    void exitStatusChanged();
    void finished(int exitCode, QProcess::ExitStatus exitStatus);

private Q_SLOTS:
    void onProcessFinished(int exitCode, QProcess::ExitStatus exitStatus);
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
    // Stateful UTF-8 → UTF-16 decoders that retain partial multi-byte
    // sequences across `readyRead*` chunk boundaries. Without this, a
    // codepoint that straddles two read chunks (common at 4 KiB
    // boundaries for non-ASCII output) would decode to U+FFFD on each
    // side. Reset on each new process invocation.
    QStringDecoder m_stdoutDecoder{QStringConverter::Utf8};
    QStringDecoder m_stderrDecoder{QStringConverter::Utf8};
    int m_exitCode = 0;
    QProcess::ExitStatus m_exitStatus = QProcess::NormalExit;
    QProcess* m_process = nullptr;
    QTimer* m_timer = nullptr;
};

} // namespace PhosphorShell
