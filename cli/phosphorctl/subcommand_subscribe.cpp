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
    // the pipe is full the byte is dropped, which is fine, even
    // one pending byte is enough to wake the loop. EINTR is the
    // only condition we retry on.
    while (::write(g_signalFd[1], &byte, sizeof(byte)) == -1 && errno == EINTR) {
        // Retry on EINTR; ignore EAGAIN (pipe full = already woken).
    }
    errno = savedErrno;
}

// Tear down anything installSignalHandlers may have set up
// partway, used as the failure-path cleanup so we don't leak fds
// or leave sigaction half-installed when an init step fails.
void teardownSignalHandlers(bool sigintInstalled, bool sigtermInstalled)
{
    if (sigintInstalled) {
        struct sigaction sa{};
        sa.sa_handler = SIG_DFL;
        sigemptyset(&sa.sa_mask);
        ::sigaction(SIGINT, &sa, nullptr);
    }
    if (sigtermInstalled) {
        struct sigaction sa{};
        sa.sa_handler = SIG_DFL;
        sigemptyset(&sa.sa_mask);
        ::sigaction(SIGTERM, &sa, nullptr);
    }
    for (int& fd : g_signalFd) {
        if (fd != -1) {
            ::close(fd);
            fd = -1;
        }
    }
}

bool installSignalHandlers()
{
    if (::pipe(g_signalFd) != 0) {
        return false;
    }
    // FD_CLOEXEC on both ends so the pipe doesn't leak into a
    // future exec'd child; O_NONBLOCK on the write end so the
    // handler can never block in ::write. Treat any fcntl failure
    // as fatal so the comment on signalHandler() above stays
    // accurate; if O_NONBLOCK fails to apply, the handler is no
    // longer guaranteed non-blocking and a full pipe could deadlock
    // a signal-interrupted thread.
    for (int fd : g_signalFd) {
        const int flags = ::fcntl(fd, F_GETFD);
        if (flags == -1 || ::fcntl(fd, F_SETFD, flags | FD_CLOEXEC) == -1) {
            teardownSignalHandlers(false, false);
            return false;
        }
    }
    const int writeFlags = ::fcntl(g_signalFd[1], F_GETFL);
    if (writeFlags == -1 || ::fcntl(g_signalFd[1], F_SETFL, writeFlags | O_NONBLOCK) == -1) {
        teardownSignalHandlers(false, false);
        return false;
    }
    // sigaction over std::signal: std::signal's re-arm semantics
    // are implementation-defined (BSD vs SysV historical split).
    // sigaction is portable. SA_RESTART avoids interrupting
    // syscalls in the main thread when SIGINT fires.
    //
    // Zero-init via {} so internal/reserved fields (sa_restorer
    // on glibc, padding) don't carry stack garbage into
    // sigaction(2); the man page explicitly warns against partial
    // init.
    struct sigaction sa{};
    sa.sa_handler = signalHandler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    if (::sigaction(SIGINT, &sa, nullptr) != 0) {
        teardownSignalHandlers(false, false);
        return false;
    }
    if (::sigaction(SIGTERM, &sa, nullptr) != 0) {
        // SIGINT install succeeded; restore it to default before
        // returning so the partial install doesn't leak.
        teardownSignalHandlers(true, false);
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
    req.insert(QLatin1String(PhosphorIpc::Field::Type), QLatin1String(PhosphorIpc::RequestType::Subscribe));
    req.insert(QLatin1String(PhosphorIpc::Field::Id), static_cast<double>(SubscriptionId));
    req.insert(QLatin1String(PhosphorIpc::Field::Target), target);
    req.insert(QLatin1String(PhosphorIpc::Field::Signal), signalName);

    const auto firstResp = client.request(req);
    if (!firstResp.has_value()) {
        err << "phosphorctl subscribe: " << client.errorMessage() << "\n";
        return 2;
    }
    if (firstResp->value(QLatin1String(PhosphorIpc::Field::Type)).toString()
        == QLatin1String(PhosphorIpc::ResponseType::Error)) {
        err << "phosphorctl subscribe: "
            << sanitiseForTerminal(firstResp->value(QLatin1String(PhosphorIpc::Field::Code)).toString()) << ": "
            << sanitiseForTerminal(firstResp->value(QLatin1String(PhosphorIpc::Field::Message)).toString()) << "\n";
        return 3;
    }

    if (!installSignalHandlers()) {
        err << "phosphorctl subscribe: failed to install signal handlers (errno=" << errno << ")\n";
        return 4;
    }

    QLocalSocket* socket = client.socket();
    // Seed the local buffer with any bytes Client::request() saw
    // AFTER the subscribe ack but BEFORE returning. Without this,
    // any event the server broadcast in the same readyRead burst
    // as the subscribe ack would be silently dropped. takePendingBytes
    // is a one-shot handoff.
    QByteArray buffer = client.takePendingBytes();
    bool serverDisconnected = false;
    // Set to true by the SIGINT/SIGTERM handler before we send
    // unsubscribe so the eventual exit code reflects "user asked
    // to stop" rather than "server died" if both fire in the same
    // dispatch (Ctrl+C immediately followed by a server shutdown).
    bool userRequestedQuit = false;
    constexpr qint64 UnsubscribeRequestId = 2;
    constexpr int UnsubscribeFlushMs = 500;
    constexpr int UnsubscribeReplyWaitMs = 200;

    auto consumeBuffer = [&buffer, &out, &err]() {
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
            // shape. QJsonDocument::toJson escapes control bytes per
            // the JSON spec, so the wire payload is terminal-safe
            // even when the payload contains attacker-controlled
            // strings (no separate sanitiseForTerminal call needed
            // for the compact JSON path).
            out << QString::fromUtf8(doc.toJson(QJsonDocument::Compact)) << "\n";
            out.flush();
        }
    };

    // Flush any frames that landed during the request() call (one
    // or more events from the same readyRead burst as the ack).
    consumeBuffer();

    QObject::connect(socket, &QLocalSocket::readyRead, [socket, &buffer, &consumeBuffer]() {
        buffer.append(socket->readAll());
        consumeBuffer();
    });

    QObject::connect(socket, &QLocalSocket::disconnected, [&serverDisconnected, &userRequestedQuit]() {
        // Only flag "server died" when the disconnect arrives
        // BEFORE the user requested a quit. The Ctrl+C path also
        // observes a disconnect (after we send unsubscribe and the
        // server cleanly closes), and we don't want that to be
        // reported as a server-side death.
        if (!userRequestedQuit) {
            serverDisconnected = true;
        }
        QCoreApplication::quit();
    });

    // Close the gap between takePendingBytes() above and the
    // readyRead slot wire-up: any bytes that arrived in the kernel
    // buffer between those two points would only be delivered on
    // the NEXT readyRead edge, so kick the drain manually here.
    buffer.append(socket->readAll());
    consumeBuffer();

    // Wake the loop on SIGINT/SIGTERM, send a clean unsubscribe,
    // wait for the kernel buffer to drain so the server actually
    // receives it, then quit.
    QSocketNotifier signalNotifier(g_signalFd[0], QSocketNotifier::Read);
    QObject::connect(&signalNotifier, &QSocketNotifier::activated, [socket, &userRequestedQuit]() {
        // Set userRequestedQuit BEFORE anything else can fire the
        // disconnected slot. Even the signal-pipe drain below pumps
        // no events on its own, but the disconnected slot can be
        // delivered asynchronously by a peer close that happened
        // before we got here; without this ordering, that
        // disconnect would set serverDisconnected=true and the
        // exit code would mis-report a Ctrl+C as "server died".
        userRequestedQuit = true;
        char drain[16];
        // Drain whatever the handler queued. Retry on EINTR (a
        // syscall interrupted by a signal during the drain is a
        // transient condition, not a stop signal); exit the loop
        // on EAGAIN/EWOULDBLOCK (pipe empty) or any other terminal
        // error.
        while (true) {
            const ssize_t n = ::read(g_signalFd[0], drain, sizeof(drain));
            if (n > 0) {
                continue;
            }
            if (n == -1 && errno == EINTR) {
                continue;
            }
            break;
        }
        QJsonObject unsub;
        unsub.insert(QLatin1String(PhosphorIpc::Field::Type), QLatin1String(PhosphorIpc::RequestType::Unsubscribe));
        unsub.insert(QLatin1String(PhosphorIpc::Field::Id), static_cast<double>(UnsubscribeRequestId));
        unsub.insert(QLatin1String(PhosphorIpc::Field::SubscriptionId), static_cast<double>(SubscriptionId));
        socket->write(PhosphorIpc::writeLine(unsub));
        // QLocalSocket::flush is non-blocking on Linux. A short
        // waitForBytesWritten actually drains the kernel buffer
        // so the server receives the unsubscribe before we exit.
        // Without this the unsubscribe is best-effort: the kernel
        // discards the pending write if the process exits first.
        socket->waitForBytesWritten(UnsubscribeFlushMs);
        // Briefly wait for the server's unsubscribe reply so the
        // server sees a clean protocol exchange rather than a
        // best-effort write followed by an immediate close. The
        // wait is bounded; if the reply doesn't arrive we still
        // quit (the unsubscribe was already delivered above).
        socket->waitForReadyRead(UnsubscribeReplyWaitMs);
        QCoreApplication::quit();
    });

    // If the server disconnected between the subscribe ack and
    // here (e.g. the daemon shut down mid-flight), the disconnected
    // lambda above would call quit() BEFORE exec() entered, leaving
    // the loop stuck waiting for an event that already happened.
    // Short-circuit that race by checking state up front; otherwise
    // run the loop.
    int rc = 0;
    if (socket->state() != QLocalSocket::ConnectedState) {
        serverDisconnected = true;
    } else {
        rc = QCoreApplication::exec();
    }
    if (rc != 0) {
        return rc;
    }
    // Server-initiated disconnect exits 2 so scripts driving us
    // can distinguish "Ctrl+C clean stop" from "server died".
    return serverDisconnected ? 2 : 0;
}

} // namespace Phosphorctl
