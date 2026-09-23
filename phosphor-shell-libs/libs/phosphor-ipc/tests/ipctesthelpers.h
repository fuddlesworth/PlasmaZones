// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

// Shared NDJSON drain / round-trip helpers used by several
// phosphor-ipc test binaries. Header-only because each test
// binary links its own QTest main and we don't want a shared
// .cpp dragging extra symbols into every binary's MOC.

#include <PhosphorIpc/IpcProtocol.h>
#include <PhosphorIpc/IpcRouter.h>

#include <QByteArray>
#include <QDir>
#include <QEventLoop>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocalSocket>
#include <QObject>
#include <QString>
#include <QTemporaryDir>
#include <QTimer>

#include <optional>

namespace PhosphorIpcTests {

// Drain a QLocalSocket until N response lines arrive or timeout
// fires. Returns the parsed objects in arrival order. Pre-drains
// bytes already buffered so a second call against the same socket
// doesn't deadlock waiting for a readyRead that already fired.
inline QList<QJsonObject> readLines(QLocalSocket& socket, int expectedCount, int timeoutMs = 2000)
{
    QList<QJsonObject> out;
    QByteArray buffer;

    auto drain = [&]() {
        buffer.append(socket.readAll());
        while (true) {
            const int nl = buffer.indexOf('\n');
            if (nl < 0) {
                break;
            }
            QByteArray line = buffer.left(nl);
            buffer.remove(0, nl + 1);
            while (!line.isEmpty() && line.endsWith('\r')) {
                line.chop(1);
            }
            if (line.isEmpty()) {
                continue;
            }
            out.append(QJsonDocument::fromJson(line).object());
        }
    };

    drain();
    if (out.size() >= expectedCount) {
        return out;
    }

    QEventLoop loop;
    QTimer timeout;
    timeout.setSingleShot(true);
    QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
    QObject::connect(&socket, &QLocalSocket::readyRead, &loop, [&] {
        drain();
        if (out.size() >= expectedCount) {
            loop.quit();
        }
    });
    timeout.start(timeoutMs);
    loop.exec();
    return out;
}

// Build a wire-format NDJSON request object. Optional fields are
// omitted when empty; the protocol parser treats absent fields the
// same as missing ones.
inline QJsonObject makeReq(const QString& type, qint64 id, const QString& target = {}, const QString& signal = {},
                           qint64 subId = 0)
{
    QJsonObject o;
    o.insert(QStringLiteral("type"), type);
    o.insert(QStringLiteral("id"), static_cast<double>(id));
    if (!target.isEmpty()) {
        o.insert(QStringLiteral("target"), target);
    }
    if (!signal.isEmpty()) {
        o.insert(QStringLiteral("signal"), signal);
    }
    if (subId != 0) {
        o.insert(QStringLiteral("subscriptionId"), static_cast<double>(subId));
    }
    return o;
}

// Build a wire-format call request (type/id/target/fn/args). The args
// array is appended verbatim; pass {} for a no-arg call.
inline QJsonObject makeCallReq(qint64 id, const QString& target, const QString& fn, const QJsonArray& args = {})
{
    QJsonObject o;
    o.insert(QStringLiteral("type"), QStringLiteral("call"));
    o.insert(QStringLiteral("id"), static_cast<double>(id));
    o.insert(QStringLiteral("target"), target);
    o.insert(QStringLiteral("fn"), fn);
    if (!args.isEmpty()) {
        o.insert(QStringLiteral("args"), args);
    }
    return o;
}

// Per-test router fixture: an isolated socket dir + path + router. Collapses
// the QTemporaryDir/sockPath/IpcRouter boilerplate every e2e/subscribe test
// otherwise repeats. Tests register their targets and call
// `router.start(fx.sockPath)` themselves (target sets vary per test).
struct RouterFixture
{
    QTemporaryDir dir;
    QString sockPath;
    PhosphorIpc::IpcRouter router;

    RouterFixture()
        : sockPath(QDir(dir.path()).filePath(QStringLiteral("test.sock")))
    {
    }
    bool valid() const
    {
        return dir.isValid();
    }
};

// One-shot connect + send + drain a single response line. Failures
// (connect timeout, response timeout) populate outError and return
// std::nullopt so callers can QFAIL with a precise diagnostic
// instead of a downstream "compared empty object to expected reply".
inline std::optional<QJsonObject> roundtrip(const QString& socketPath, const QJsonObject& request, QString* outError,
                                            int timeoutMs = 2000)
{
    QLocalSocket socket;
    QByteArray buffer;
    QEventLoop loop;
    QTimer timeout;
    timeout.setSingleShot(true);

    QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
    QObject::connect(&socket, &QLocalSocket::readyRead, &loop, [&] {
        buffer.append(socket.readAll());
        if (buffer.contains('\n')) {
            loop.quit();
        }
    });

    socket.connectToServer(socketPath);
    if (!socket.waitForConnected(timeoutMs)) {
        if (outError) {
            *outError = QStringLiteral("connect failed: %1").arg(socket.errorString());
        }
        return std::nullopt;
    }
    socket.write(PhosphorIpc::writeLine(request));
    socket.flush();

    timeout.start(timeoutMs);
    loop.exec();

    if (!buffer.contains('\n')) {
        if (outError) {
            *outError = QStringLiteral("no response within %1ms").arg(timeoutMs);
        }
        return std::nullopt;
    }
    const int nl = buffer.indexOf('\n');
    QByteArray line = buffer.left(nl);
    while (!line.isEmpty() && line.endsWith('\r')) {
        line.chop(1);
    }
    socket.disconnectFromServer();
    return QJsonDocument::fromJson(line).object();
}

} // namespace PhosphorIpcTests
