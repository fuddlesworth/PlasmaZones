// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
#include "shellcheatsheetbridge.h"

#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocalSocket>
#include <QLoggingCategory>
#include <utility>

namespace PlasmaZones {
namespace {
Q_LOGGING_CATEGORY(lcCheatsheetBridge, "plasmazones.cheatsheetbridge")
constexpr int ReplyLimit = 64 * 1024;
}

ShellCheatsheetBridge::ShellCheatsheetBridge(QObject* parent)
    : ShellCheatsheetBridge(socketPath(), 1000, parent)
{
}

ShellCheatsheetBridge::ShellCheatsheetBridge(QString path, int timeoutMs, QObject* parent)
    : QObject(parent)
    , m_socketPath(std::move(path))
    , m_timeoutMs(qMax(1, timeoutMs))
{
    m_timeout.setSingleShot(true);
    connect(&m_timeout, &QTimer::timeout, this, [this] {
        finish(m_sent ? Result::Uncertain : Result::Unavailable);
    });
}

QString ShellCheatsheetBridge::socketPath()
{
    const QString override = qEnvironmentVariable("PHOSPHOR_SOCKET");
    if (!override.isEmpty())
        return override;
    const QString runtime = qEnvironmentVariable("XDG_RUNTIME_DIR");
    return runtime.isEmpty() ? QString() : QDir(runtime).filePath(QStringLiteral("phosphor.sock"));
}

ShellCheatsheetBridge::~ShellCheatsheetBridge()
{
    cancel();
}

void ShellCheatsheetBridge::toggle(const QString& screenId, const QString& screenName)
{
    if (screenId.isEmpty() || screenName.isEmpty())
        return;
    // Two still-unsent toggles on the same output cancel each other. Bound
    // queued work during a stalled peer without changing toggle parity.
    if (!m_requests.isEmpty() && m_requests.back().screenId == screenId) {
        m_requests.removeLast();
        return;
    }
    if (m_requests.size() >= 32) {
        qCWarning(lcCheatsheetBridge) << "Shortcut reference request queue is full";
        return;
    }
    m_requests.enqueue({screenId, screenName});
    startNext();
}

void ShellCheatsheetBridge::startNext()
{
    if (m_pending || m_requests.isEmpty())
        return;
    m_current = m_requests.dequeue();
    m_pending = true;
    m_sent = false;
    m_buffer.clear();
    ++m_requestId;
    if (m_socketPath.isEmpty()) {
        finish(Result::Unavailable);
        return;
    }
    auto* socket = new QLocalSocket(this);
    m_socket = socket;
    connect(socket, &QLocalSocket::connected, this, [this, socket] {
        if (m_socket != socket)
            return;
        // Phosphor IPC v1 NDJSON call, the same target registry used by
        // phosphorctl. Its CLI client is synchronous and cannot run here.
        const QJsonObject request{{QStringLiteral("type"), QStringLiteral("call")},
                                  {QStringLiteral("id"), m_requestId},
                                  {QStringLiteral("target"), QStringLiteral("cheatsheet")},
                                  {QStringLiteral("fn"), QStringLiteral("toggleForScreen")},
                                  {QStringLiteral("args"), QJsonArray{m_current.screenName}}};
        const QByteArray line = QJsonDocument(request).toJson(QJsonDocument::Compact) + '\n';
        m_sent = socket->write(line) == line.size();
        if (!m_sent)
            finish(Result::Uncertain);
    });
    connect(socket, &QLocalSocket::readyRead, this, [this, socket] {
        if (m_socket == socket)
            readReply();
    });
    const auto disconnected = [this, socket] {
        if (m_socket == socket)
            finish(m_sent ? Result::Uncertain : Result::Unavailable);
    };
    connect(socket, &QLocalSocket::disconnected, this, disconnected);
    connect(socket, &QLocalSocket::errorOccurred, this, disconnected);
    m_timeout.start(m_timeoutMs);
    socket->connectToServer(m_socketPath);
}

void ShellCheatsheetBridge::readReply()
{
    m_buffer += m_socket->readAll();
    if (m_buffer.size() > ReplyLimit) {
        finish(Result::Uncertain);
        return;
    }
    for (qsizetype end; (end = m_buffer.indexOf('\n')) >= 0;) {
        const QByteArray line = m_buffer.left(end);
        m_buffer.remove(0, end + 1);
        QJsonParseError error;
        const QJsonDocument document = QJsonDocument::fromJson(line, &error);
        if (error.error != QJsonParseError::NoError || !document.isObject()) {
            finish(Result::Uncertain);
            return;
        }
        const QJsonObject reply = document.object();
        const QJsonValue id = reply.value(QStringLiteral("id"));
        if (!id.isDouble() || id.toDouble() != double(m_requestId))
            continue;
        const QString type = reply.value(QStringLiteral("type")).toString();
        const QJsonValue result = reply.value(QStringLiteral("result"));
        if (type == QLatin1String("reply") && result.isBool()) {
            finish(result.toBool() ? Result::Accepted : Result::Unavailable);
        } else if (type == QLatin1String("error")
                   && (reply.value(QStringLiteral("code")).toString() == QLatin1String("NO_SUCH_TARGET")
                       || reply.value(QStringLiteral("code")).toString() == QLatin1String("NO_SUCH_FN"))) {
            finish(Result::Unavailable);
        } else {
            finish(Result::Uncertain);
        }
        return;
    }
}

void ShellCheatsheetBridge::discardSocket()
{
    if (!m_socket)
        return;
    auto* socket = m_socket.data();
    m_socket = nullptr;
    socket->disconnect(this);
    socket->abort();
    socket->deleteLater();
}

void ShellCheatsheetBridge::finish(Result result)
{
    if (!m_pending)
        return;
    const QString screenId = m_current.screenId;
    m_pending = false;
    m_timeout.stop();
    discardSocket();
    if (result == Result::Accepted)
        Q_EMIT accepted();
    else if (result == Result::Unavailable)
        Q_EMIT fallbackRequested(screenId);
    else
        // Once sent, a lost reply does not prove the shell did not open.
        // Never open a second overlay based on that ambiguous outcome.
        qCWarning(lcCheatsheetBridge) << "Shell shortcut reference did not confirm the request";
    QTimer::singleShot(0, this, &ShellCheatsheetBridge::startNext);
}

void ShellCheatsheetBridge::cancel()
{
    m_requests.clear();
    m_pending = false;
    m_timeout.stop();
    discardSocket();
    m_buffer.clear();
}

} // namespace PlasmaZones
