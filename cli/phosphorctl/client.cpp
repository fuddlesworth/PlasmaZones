// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "client.h"

#include <PhosphorIpc/IpcProtocol.h>

#include <QByteArray>
#include <QDir>
#include <QEventLoop>
#include <QJsonDocument>
#include <QProcessEnvironment>
#include <QTimer>

namespace Phosphorctl {

Client::Client(QObject* parent)
    : QObject(parent)
{
}

QString Client::resolveSocketPath(const QString& cliArg)
{
    if (!cliArg.isEmpty()) {
        return cliArg;
    }
    const QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    const QString fromEnv = env.value(QStringLiteral("PHOSPHOR_SOCKET"));
    if (!fromEnv.isEmpty()) {
        return fromEnv;
    }
    const QString xdg = env.value(QStringLiteral("XDG_RUNTIME_DIR"));
    if (xdg.isEmpty()) {
        return {};
    }
    return QDir(xdg).filePath(QStringLiteral("phosphor.sock"));
}

bool Client::connectTo(const QString& socketPath, int timeoutMs)
{
    if (socketPath.isEmpty()) {
        m_errorMessage = QStringLiteral("no socket path resolved (set --socket, PHOSPHOR_SOCKET, or XDG_RUNTIME_DIR)");
        return false;
    }
    // Reset any prior connection so retry-after-failure callers
    // don't leak a half-open socket. abort() is QLocalSocket's
    // documented hard-reset.
    if (m_socket.state() != QLocalSocket::UnconnectedState) {
        m_socket.abort();
    }
    m_socket.connectToServer(socketPath);
    if (!m_socket.waitForConnected(timeoutMs)) {
        m_errorMessage = QStringLiteral("connect to '%1' failed: %2").arg(socketPath, m_socket.errorString());
        return false;
    }
    return true;
}

std::optional<QJsonObject> Client::request(const QJsonObject& req, int timeoutMs)
{
    if (m_socket.state() != QLocalSocket::ConnectedState) {
        m_errorMessage = QStringLiteral("not connected");
        return std::nullopt;
    }

    // Extract the request id once so we can correlate the matching
    // response. A response whose id doesn't match (e.g. a stray
    // event from an unrelated subscription that landed before our
    // reply) is preserved in m_readBuffer for takePendingBytes to
    // hand off to the streaming-subscribe loop. Discarding events
    // here would drop any broadcast that piggy-backed onto the
    // subscribe ack's readyRead.
    const qint64 expectedId = static_cast<qint64>(req.value(QString::fromUtf8(PhosphorIpc::Field::Id)).toDouble(0));

    // QLocalSocket::waitForReadyRead on Linux doesn't reliably pump
    // the peer's accept / write loop. Use an explicit QEventLoop
    // with a QTimer deadline (same pattern as the e2e tests).
    std::optional<QJsonObject> matched;
    QEventLoop loop;
    QTimer timeout;
    timeout.setSingleShot(true);
    QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);

    auto drain = [&]() {
        m_readBuffer.append(m_socket.readAll());
        // Scan complete lines without consuming them eagerly. Once
        // we find a line that matches expectedId, we remove every
        // byte up to and including that line; any later bytes stay
        // in m_readBuffer for takePendingBytes to surface to the
        // subscribe loop.
        int cursor = 0;
        while (true) {
            const int nl = m_readBuffer.indexOf('\n', cursor);
            if (nl < 0) {
                return;
            }
            QByteArray line = m_readBuffer.mid(cursor, nl - cursor);
            const int lineEnd = nl + 1;
            while (!line.isEmpty() && line.endsWith('\r')) {
                line.chop(1);
            }
            if (line.isEmpty()) {
                cursor = lineEnd;
                continue;
            }
            QJsonParseError err{};
            const QJsonDocument doc = QJsonDocument::fromJson(line, &err);
            if (err.error != QJsonParseError::NoError || !doc.isObject()) {
                // Malformed mid-stream frame. Drop just that line
                // from the buffer and keep scanning.
                m_readBuffer.remove(cursor, lineEnd - cursor);
                continue;
            }
            const QJsonObject obj = doc.object();
            // An absent `id` is the server's "no client correlation"
            // sentinel, see buildError(0,...) which omits the id
            // field. Don't match those against a client request
            // that happens to use id=0; they're parse-level errors
            // unrelated to any specific request. Drop them from the
            // buffer rather than feeding them to the subscribe loop.
            if (!obj.contains(QString::fromUtf8(PhosphorIpc::Field::Id))) {
                m_readBuffer.remove(cursor, lineEnd - cursor);
                continue;
            }
            const qint64 respId = static_cast<qint64>(obj.value(QString::fromUtf8(PhosphorIpc::Field::Id)).toDouble(0));
            if (respId == expectedId) {
                matched = obj;
                // Consume everything up to and including the matched
                // line. Anything after it (events that piggy-backed
                // onto the same readyRead burst) stays in
                // m_readBuffer for takePendingBytes to surface.
                m_readBuffer.remove(0, lineEnd);
                loop.quit();
                return;
            }
            // Non-matching id: leave the line in m_readBuffer for
            // the subscribe handoff and advance the cursor past it.
            cursor = lineEnd;
        }
    };

    auto onReadyRead = QObject::connect(&m_socket, &QLocalSocket::readyRead, &loop, drain);
    auto onDisconnected = QObject::connect(&m_socket, &QLocalSocket::disconnected, &loop, &QEventLoop::quit);

    m_socket.write(PhosphorIpc::writeLine(req));
    m_socket.flush();

    // Drain any bytes that arrived before our readyRead handler
    // was connected. Without this the loop can deadlock when the
    // server's reply landed between the connectToServer call and
    // the QObject::connect above (rare but possible on a busy
    // socket).
    drain();
    if (!matched) {
        timeout.start(timeoutMs);
        loop.exec();
    }

    QObject::disconnect(onReadyRead);
    QObject::disconnect(onDisconnected);

    if (!matched) {
        if (m_socket.state() != QLocalSocket::ConnectedState) {
            m_errorMessage = QStringLiteral("server disconnected before responding");
        } else {
            m_errorMessage = QStringLiteral("timed out waiting for response (%1ms)").arg(timeoutMs);
        }
        return std::nullopt;
    }
    return matched;
}

QString Client::errorMessage() const
{
    return m_errorMessage;
}

QLocalSocket* Client::socket()
{
    return &m_socket;
}

QByteArray Client::takePendingBytes()
{
    QByteArray pending;
    pending.swap(m_readBuffer);
    return pending;
}

} // namespace Phosphorctl
