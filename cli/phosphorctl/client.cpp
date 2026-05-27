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

    // QLocalSocket::waitForReadyRead on Linux doesn't reliably pump
    // the peer's accept / write loop. Use an explicit QEventLoop
    // with a QTimer deadline (same pattern as the e2e tests).
    QByteArray buffer;
    QEventLoop loop;
    QTimer timeout;
    timeout.setSingleShot(true);
    QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
    auto onReadyRead = QObject::connect(&m_socket, &QLocalSocket::readyRead, &loop, [&] {
        buffer.append(m_socket.readAll());
        if (buffer.contains('\n')) {
            loop.quit();
        }
    });
    auto onDisconnected = QObject::connect(&m_socket, &QLocalSocket::disconnected, &loop, &QEventLoop::quit);

    m_socket.write(PhosphorIpc::writeLine(req));
    m_socket.flush();
    timeout.start(timeoutMs);
    loop.exec();

    QObject::disconnect(onReadyRead);
    QObject::disconnect(onDisconnected);

    if (!buffer.contains('\n')) {
        if (m_socket.state() != QLocalSocket::ConnectedState) {
            m_errorMessage = QStringLiteral("server disconnected before responding");
        } else {
            m_errorMessage = QStringLiteral("timed out waiting for response (%1ms)").arg(timeoutMs);
        }
        return std::nullopt;
    }
    const int nl = buffer.indexOf('\n');
    QByteArray line = buffer.left(nl);
    while (!line.isEmpty() && line.endsWith('\r')) {
        line.chop(1);
    }
    QJsonParseError err{};
    const QJsonDocument doc = QJsonDocument::fromJson(line, &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) {
        m_errorMessage = QStringLiteral("malformed response: %1").arg(err.errorString());
        return std::nullopt;
    }
    return doc.object();
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
