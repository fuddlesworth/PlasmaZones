// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QObject>
#include <QByteArray>
#include <QPointer>
#include <QQueue>
#include <QTimer>

class QLocalSocket;

namespace PlasmaZones {

// Uses the optional shell's registered cheatsheet IPC target. It deliberately
// depends only on Qt: the daemon also builds without Phosphor shell libraries.
class ShellCheatsheetBridge : public QObject
{
    Q_OBJECT
public:
    explicit ShellCheatsheetBridge(QObject* parent = nullptr);
    ShellCheatsheetBridge(QString socketPath, int timeoutMs, QObject* parent = nullptr);
    ~ShellCheatsheetBridge() override;
    void toggle(const QString& screenId, const QString& screenName);
    void cancel();
    static QString socketPath();

Q_SIGNALS:
    void accepted();
    void fallbackRequested(const QString& screenId);

private:
    enum class Result {
        Accepted,
        Unavailable,
        Uncertain
    };
    struct Request
    {
        QString screenId;
        QString screenName;
    };
    void startNext();
    void readReply();
    void finish(Result result);
    void discardSocket();

    const QString m_socketPath;
    const int m_timeoutMs;
    QPointer<QLocalSocket> m_socket;
    QTimer m_timeout;
    QQueue<Request> m_requests;
    Request m_current;
    QByteArray m_buffer;
    qint64 m_requestId = 0;
    bool m_pending = false;
    bool m_sent = false;
};

} // namespace PlasmaZones
