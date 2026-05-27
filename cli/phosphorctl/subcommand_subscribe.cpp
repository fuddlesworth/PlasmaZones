// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "subcommands.h"

#include "client.h"

#include <PhosphorIpc/IpcProtocol.h>

#include <QByteArray>
#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QLocalSocket>
#include <QObject>
#include <QSocketNotifier>
#include <QString>
#include <QStringList>
#include <QTextStream>

#include <csignal>
#include <unistd.h>

namespace Phosphorctl {

namespace {

// Self-pipe used by the SIGINT / SIGTERM handler to wake the Qt
// event loop. signal() handlers can't safely touch Qt state, but
// they can write a byte to a pipe; the QSocketNotifier on the
// read end wakes the loop and lets us call QCoreApplication::quit
// from a normal Qt slot.
int g_signalFd[2] = {-1, -1};

void signalHandler(int /*signum*/)
{
    char byte = 1;
    // Intentionally unchecked write — handler-safe.
    ssize_t r = ::write(g_signalFd[1], &byte, sizeof(byte));
    Q_UNUSED(r);
}

bool installSignalHandlers()
{
    if (::pipe(g_signalFd) != 0) {
        return false;
    }
    std::signal(SIGINT, signalHandler);
    std::signal(SIGTERM, signalHandler);
    return true;
}

} // namespace

int runSubscribe(const QStringList& args, const QString& socketPath)
{
    QTextStream out(stdout);
    QTextStream err(stderr);

    if (args.size() != 1) {
        err << "phosphorctl subscribe: expects exactly one argument: <target>.<signal>\n";
        return 1;
    }
    const QString targetDotSig = args.first();
    const int dotIdx = targetDotSig.indexOf(QLatin1Char('.'));
    if (dotIdx <= 0 || dotIdx == targetDotSig.size() - 1) {
        err << "phosphorctl subscribe: argument must be in the form <target>.<signal>\n";
        return 1;
    }
    const QString target = targetDotSig.left(dotIdx);
    const QString signalName = targetDotSig.mid(dotIdx + 1);

    Client client;
    if (!client.connectTo(socketPath)) {
        err << "phosphorctl subscribe: " << client.errorMessage() << "\n";
        return 2;
    }

    // Send the subscribe request. The server replies once with a
    // {"type":"reply"} confirming the subscription is live; then
    // streams events tagged with subscriptionId == request id
    // until either side disconnects.
    constexpr qint64 SubscriptionId = 1;
    QJsonObject req;
    req.insert(QStringLiteral("type"), QString::fromUtf8(PhosphorIpc::RequestType::Subscribe));
    req.insert(QStringLiteral("id"), SubscriptionId);
    req.insert(QStringLiteral("target"), target);
    req.insert(QStringLiteral("signal"), signalName);

    const auto firstResp = client.request(req);
    if (!firstResp.has_value()) {
        err << "phosphorctl subscribe: " << client.errorMessage() << "\n";
        return 2;
    }
    if (firstResp->value(QStringLiteral("type")).toString() == QString::fromUtf8(PhosphorIpc::ResponseType::Error)) {
        err << "phosphorctl subscribe: " << firstResp->value(QStringLiteral("code")).toString() << ": "
            << firstResp->value(QStringLiteral("message")).toString() << "\n";
        return 3;
    }
    // First response is the subscription-acknowledged reply; from
    // here on, we just print event payloads as they arrive.

    if (!installSignalHandlers()) {
        err << "phosphorctl subscribe: failed to install signal handlers\n";
        return 2;
    }

    QLocalSocket* socket = client.socket();
    QByteArray buffer;

    QObject::connect(socket, &QLocalSocket::readyRead, [socket, &buffer, &out]() {
        buffer.append(socket->readAll());
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
            // Pretty-print events one-per-line so the output stream
            // can be piped through `jq` or grepped without further
            // framing. Errors mid-stream (e.g. server pushing an
            // {"type":"error",...} mid-subscription) also print
            // here; the user can spot them by the "type":"error"
            // shape.
            const QJsonDocument doc = QJsonDocument::fromJson(line);
            out << QString::fromUtf8(doc.toJson(QJsonDocument::Compact)) << "\n";
            out.flush();
        }
    });

    QObject::connect(socket, &QLocalSocket::disconnected, []() {
        QCoreApplication::quit();
    });

    // Wake the loop when SIGINT/SIGTERM fires, send a clean
    // unsubscribe, then quit.
    QSocketNotifier signalNotifier(g_signalFd[0], QSocketNotifier::Read);
    QObject::connect(&signalNotifier, &QSocketNotifier::activated, [&client, socket]() {
        char drain[16];
        ssize_t r = ::read(g_signalFd[0], drain, sizeof(drain));
        Q_UNUSED(r);
        QJsonObject unsub;
        unsub.insert(QStringLiteral("type"), QString::fromUtf8(PhosphorIpc::RequestType::Unsubscribe));
        unsub.insert(QStringLiteral("id"), 2);
        unsub.insert(QStringLiteral("subscriptionId"), 1);
        socket->write(PhosphorIpc::writeLine(unsub));
        socket->flush();
        // Give the server a tick to process the unsubscribe before
        // quitting; not strictly required (socket disconnect would
        // do it) but cleaner.
        QCoreApplication::quit();
        Q_UNUSED(client);
    });

    return QCoreApplication::exec();
}

} // namespace Phosphorctl
