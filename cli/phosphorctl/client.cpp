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
    // reply) is dropped and we keep reading.
    const qint64 expectedId = static_cast<qint64>(req.value(QString::fromUtf8(PhosphorIpc::Field::Id)).toDouble(0));

    // QLocalSocket::waitForReadyRead on Linux doesn't reliably pump
    // the peer's accept / write loop. Use an explicit QEventLoop
    // with a QTimer deadline (same pattern as the e2e tests).
    QByteArray buffer;
    std::optional<QJsonObject> matched;
    QEventLoop loop;
    QTimer timeout;
    timeout.setSingleShot(true);
    QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);

    auto drain = [&]() {
        buffer.append(m_socket.readAll());
        while (true) {
            const int nl = buffer.indexOf('\n');
            if (nl < 0) {
                return;
            }
            QByteArray line = buffer.left(nl);
            buffer.remove(0, nl + 1);
            while (!line.isEmpty() && line.endsWith('\r')) {
                line.chop(1);
            }
            if (line.isEmpty()) {
                continue;
            }
            QJsonParseError err{};
            const QJsonDocument doc = QJsonDocument::fromJson(line, &err);
            if (err.error != QJsonParseError::NoError || !doc.isObject()) {
                continue;
            }
            const QJsonObject obj = doc.object();
            const qint64 respId = static_cast<qint64>(obj.value(QString::fromUtf8(PhosphorIpc::Field::Id)).toDouble(0));
            if (respId == expectedId) {
                matched = obj;
                loop.quit();
                return;
            }
            // Non-matching id: discard and keep reading.
        }
    };

    auto onReadyRead = QObject::connect(&m_socket, &QLocalSocket::readyRead, &loop, drain);
    auto onDisconnected = QObject::connect(&m_socket, &QLocalSocket::disconnected, &loop, &QEventLoop::quit);

    m_socket.write(PhosphorIpc::writeLine(req));
    m_socket.flush();
    timeout.start(timeoutMs);
    loop.exec();

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

} // namespace Phosphorctl
