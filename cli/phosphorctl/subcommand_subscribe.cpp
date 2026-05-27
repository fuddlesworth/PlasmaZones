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

#include <cerrno>
#include <csignal>
#include <fcntl.h>
#include <unistd.h>

namespace Phosphorctl {

namespace {

// Self-pipe used by the SIGINT / SIGTERM handler to wake the Qt
// event loop. Signal handlers can't safely touch Qt state, but
// they can write a byte to the pipe; the QSocketNotifier on the
// read end wakes the loop and lets us call QCoreApplication::quit
// from a normal Qt slot.
int g_signalFd[2] = {-1, -1};

void signalHandler(int /*signum*/)
{
    // Async-signal-safety rule: save+restore errno so handlers
    // don't corrupt errno on the interrupted thread mid-syscall.
    const int savedErrno = errno;
    char byte = 1;
    // Write end is non-blocking (see installSignalHandlers): if
    // the pipe is full the byte is dropped, which is fine — even
    // one pending byte is enough to wake the loop. EINTR is the
    // only condition we retry on.
    while (::write(g_signalFd[1], &byte, sizeof(byte)) == -1 && errno == EINTR) {
        // Retry on EINTR; ignore EAGAIN (pipe full = already woken).
    }
    errno = savedErrno;
}

bool installSignalHandlers()
{
    if (::pipe(g_signalFd) != 0) {
        return false;
    }
    // FD_CLOEXEC on both ends so the pipe doesn't leak into a
    // future exec'd child; O_NONBLOCK on the write end so the
    // handler can never block in ::write.
    for (int fd : g_signalFd) {
        const int flags = ::fcntl(fd, F_GETFD);
        if (flags != -1) {
            ::fcntl(fd, F_SETFD, flags | FD_CLOEXEC);
        }
    }
    const int writeFlags = ::fcntl(g_signalFd[1], F_GETFL);
    if (writeFlags != -1) {
        ::fcntl(g_signalFd[1], F_SETFL, writeFlags | O_NONBLOCK);
    }
    // sigaction over std::signal: std::signal's re-arm semantics
    // are implementation-defined (BSD vs SysV historical split).
    // sigaction is portable. SA_RESTART avoids interrupting
    // syscalls in the main thread when SIGINT fires.
    struct sigaction sa;
    sa.sa_handler = signalHandler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    if (::sigaction(SIGINT, &sa, nullptr) != 0) {
        return false;
    }
    if (::sigaction(SIGTERM, &sa, nullptr) != 0) {
        return false;
    }
    return true;
}

} // namespace

int runSubscribe(QStringList args, QString socketPath)
{
    QTextStream out(stdout);
    QTextStream err(stderr);
    const QString lateSocket = stripSocketFlag(args);
    if (!lateSocket.isEmpty()) {
        socketPath = lateSocket;
    }

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

    constexpr qint64 SubscriptionId = 1;
    QJsonObject req;
    req.insert(QString::fromUtf8(PhosphorIpc::Field::Type), QString::fromUtf8(PhosphorIpc::RequestType::Subscribe));
    req.insert(QString::fromUtf8(PhosphorIpc::Field::Id), static_cast<double>(SubscriptionId));
    req.insert(QString::fromUtf8(PhosphorIpc::Field::Target), target);
    req.insert(QString::fromUtf8(PhosphorIpc::Field::Signal), signalName);

    const auto firstResp = client.request(req);
    if (!firstResp.has_value()) {
        err << "phosphorctl subscribe: " << client.errorMessage() << "\n";
        return 2;
    }
    if (firstResp->value(QString::fromUtf8(PhosphorIpc::Field::Type)).toString()
        == QString::fromUtf8(PhosphorIpc::ResponseType::Error)) {
        err << "phosphorctl subscribe: " << firstResp->value(QString::fromUtf8(PhosphorIpc::Field::Code)).toString()
            << ": " << firstResp->value(QString::fromUtf8(PhosphorIpc::Field::Message)).toString() << "\n";
        return 3;
    }

    if (!installSignalHandlers()) {
        err << "phosphorctl subscribe: failed to install signal handlers (errno=" << errno << ")\n";
        return 4;
    }

    QLocalSocket* socket = client.socket();
    QByteArray buffer;
    bool serverDisconnected = false;

    QObject::connect(socket, &QLocalSocket::readyRead, [socket, &buffer, &out, &err]() {
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
            QJsonParseError parseErr{};
            const QJsonDocument doc = QJsonDocument::fromJson(line, &parseErr);
            if (parseErr.error != QJsonParseError::NoError || !doc.isObject()) {
                err << "phosphorctl subscribe: ignoring malformed line: " << parseErr.errorString() << "\n";
                err.flush();
                continue;
            }
            // Pretty-print events one-per-line so the stream can
            // be piped through `jq` or grepped without further
            // framing. Mid-stream {"type":"error",...} frames also
            // print here; the user spots them by the "type":"error"
            // shape.
            out << QString::fromUtf8(doc.toJson(QJsonDocument::Compact)) << "\n";
            out.flush();
        }
    });

    QObject::connect(socket, &QLocalSocket::disconnected, [&serverDisconnected]() {
        serverDisconnected = true;
        QCoreApplication::quit();
    });

    // Wake the loop on SIGINT/SIGTERM, send a clean unsubscribe,
    // wait for the kernel buffer to drain so the server actually
    // receives it, then quit.
    QSocketNotifier signalNotifier(g_signalFd[0], QSocketNotifier::Read);
    QObject::connect(&signalNotifier, &QSocketNotifier::activated, [socket]() {
        char drain[16];
        // Drain whatever the handler queued; ignore EAGAIN/EWOULDBLOCK.
        while (::read(g_signalFd[0], drain, sizeof(drain)) > 0) { }
        QJsonObject unsub;
        unsub.insert(QString::fromUtf8(PhosphorIpc::Field::Type),
                     QString::fromUtf8(PhosphorIpc::RequestType::Unsubscribe));
        unsub.insert(QString::fromUtf8(PhosphorIpc::Field::Id), 2);
        unsub.insert(QString::fromUtf8(PhosphorIpc::Field::SubscriptionId), 1);
        socket->write(PhosphorIpc::writeLine(unsub));
        // QLocalSocket::flush is non-blocking on Linux. A short
        // waitForBytesWritten actually drains the kernel buffer
        // so the server receives the unsubscribe before we exit.
        // Without this the unsubscribe is best-effort: the kernel
        // discards the pending write if the process exits first.
        socket->waitForBytesWritten(500);
        QCoreApplication::quit();
    });

    const int rc = QCoreApplication::exec();
    if (rc != 0) {
        return rc;
    }
    // Server-initiated disconnect exits 2 so scripts driving us
    // can distinguish "Ctrl+C clean stop" from "server died".
    return serverDisconnected ? 2 : 0;
}

} // namespace Phosphorctl
