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

std::optional<QJsonObject> Client::request(const QJsonObject& req, int timeoutMs,
                                           const std::function<void(const QJsonObject&)>& onEvent)
{
    if (m_socket.state() != QLocalSocket::ConnectedState) {
        m_errorMessage = QStringLiteral("not connected");
        return std::nullopt;
    }

    // Discard any leftover bytes from a previous request(). A live
    // Client instance is expected to be either consumed by exactly
    // one request() + one takePendingBytes() handoff, or by a chain
    // of request()s with no streaming consumer. Either way, bytes
    // that survived an earlier exchange aren't meaningful to the
    // new one: they'd either match a stale id (replay) or be a
    // residual event from a subscription that was never drained.
    // Drop them rather than scan them against the new expectedId.
    m_readBuffer.clear();

    // Extract the request id once so we can correlate the matching
    // response. Frames that don't match are kept in m_readBuffer
    // for takePendingBytes to hand off to the streaming-subscribe
    // loop, so that events broadcast in the same readyRead burst as
    // the subscribe ack aren't silently dropped.
    const qint64 expectedId = static_cast<qint64>(req.value(QLatin1String(PhosphorIpc::Field::Id)).toDouble(0));

    // QLocalSocket::waitForReadyRead on Linux doesn't reliably pump
    // the peer's accept / write loop. Use an explicit QEventLoop
    // with a QTimer deadline (same pattern as the e2e tests).
    std::optional<QJsonObject> matched;
    QEventLoop loop;
    QTimer timeout;
    timeout.setSingleShot(true);
    QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);

    // Scan complete lines without consuming them eagerly. The line we
    // match for expectedId, everything before it (kept events and
    // unrelated orphan error frames) and malformed lines get dropped from
    // the buffer. Event frames after the matched line stay in m_readBuffer
    // for takePendingBytes to surface to the streaming-subscribe loop.
    // Event frames before it go to `onEvent` when one is set, and are
    // dropped there. `cursor` survives across drains: the lines before it
    // are kept events, already looked at, and a call whose target streams
    // events to its caller would otherwise re-parse every one of them on
    // each readyRead.
    int cursor = 0;
    auto drain = [&]() {
        m_readBuffer.append(m_socket.readAll());
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
            const QString frameType = obj.value(QLatin1String(PhosphorIpc::Field::Type)).toString();
            // Event frames (and any other ResponseType::Event-shaped
            // payload) carry subscriptionId, or target and signal for an
            // event sent to this connection alone, never id. With an
            // onEvent consumer they go to it now. Without one they stay in
            // m_readBuffer, the cursor moving past them, until a match
            // drops them with everything before it.
            if (frameType == QLatin1String(PhosphorIpc::ResponseType::Event)) {
                if (onEvent) {
                    m_readBuffer.remove(cursor, lineEnd - cursor);
                    onEvent(obj);
                } else {
                    cursor = lineEnd;
                }
                continue;
            }
            // Non-event frames must carry an id to be correlated.
            // An absent id on a reply / error is an uncorrelated
            // server-side parse error (buildError(0,...) shape) and
            // is dropped rather than handed to the subscribe loop.
            if (!obj.contains(QLatin1String(PhosphorIpc::Field::Id))) {
                m_readBuffer.remove(cursor, lineEnd - cursor);
                continue;
            }
            const qint64 respId = static_cast<qint64>(obj.value(QLatin1String(PhosphorIpc::Field::Id)).toDouble(0));
            if (respId == expectedId) {
                matched = obj;
                // Consume everything up to and including the matched
                // line. Anything after it (events that piggy-backed
                // onto the same readyRead burst) stays in
                // m_readBuffer for takePendingBytes to surface.
                m_readBuffer.remove(0, lineEnd);
                cursor = 0;
                loop.quit();
                return;
            }
            // Reply with a non-matching id: drop it. We don't have
            // a use for stale replies and they shouldn't pile up.
            m_readBuffer.remove(cursor, lineEnd - cursor);
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
        if (timeoutMs > 0) {
            timeout.start(timeoutMs);
        }
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
