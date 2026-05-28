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
    // O_NONBLOCK on the read end too. Without this, the drain loop
    // in the SIGINT/SIGTERM lambda would block indefinitely on its
    // second read(): the first read returns the signal byte and the
    // loop continues; the second read blocks because the pipe is
    // empty AND this process still holds the write end (so no EOF).
    // The drain loop's EINTR/EAGAIN exit conditions assume a
    // non-blocking fd; without it Ctrl+C silently hangs the CLI.
    const int readFlags = ::fcntl(g_signalFd[0], F_GETFL);
    if (readFlags == -1 || ::fcntl(g_signalFd[0], F_SETFL, readFlags | O_NONBLOCK) == -1) {
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
    QString socketErr;
    const QString lateSocket = stripSocketFlag(args, &socketErr);
    if (!socketErr.isEmpty()) {
        err << "phosphorctl subscribe: " << socketErr << "\n";
        return 1;
    }
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
            << sanitiseForSingleLine(firstResp->value(QLatin1String(PhosphorIpc::Field::Code)).toString()) << ": "
            << sanitiseForSingleLine(firstResp->value(QLatin1String(PhosphorIpc::Field::Message)).toString()) << "\n";
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
            const QJsonObject obj = doc.object();
            const QString frameType = obj.value(QLatin1String(PhosphorIpc::Field::Type)).toString();
            if (frameType == QLatin1String(PhosphorIpc::ResponseType::Event)) {
                // Pretty-print events one-per-line so the stream
                // can be piped through `jq` or grepped without
                // further framing. QJsonDocument::toJson(Compact)
                // already appends '\n', so the trimmed() form keeps
                // exactly one newline per event line (otherwise
                // we'd emit a double newline).
                out << QString::fromUtf8(doc.toJson(QJsonDocument::Compact).trimmed()) << "\n";
                out.flush();
            } else if (frameType == QLatin1String(PhosphorIpc::ResponseType::Error)) {
                // Mid-stream wire-level error: route to stderr with
                // the structured Code: Message [(detail: ...)]
                // format used by subcommand_call. Don't print it as
                // a normal event line (a downstream `jq` pipeline
                // would choke if event-shaped objects were
                // interleaved with error-shaped objects).
                err << "phosphorctl subscribe: "
                    << sanitiseForSingleLine(obj.value(QLatin1String(PhosphorIpc::Field::Code)).toString()) << ": "
                    << sanitiseForSingleLine(obj.value(QLatin1String(PhosphorIpc::Field::Message)).toString());
                const QJsonValue detail = obj.value(QLatin1String(PhosphorIpc::Field::Detail));
                if (detail.isObject() && !detail.toObject().isEmpty()) {
                    err << " (detail: "
                        << QString::fromUtf8(QJsonDocument(detail.toObject()).toJson(QJsonDocument::Compact).trimmed())
                        << ")";
                }
                err << "\n";
                err.flush();
            } else if (frameType == QLatin1String(PhosphorIpc::ResponseType::Reply)) {
                // Reply frames (e.g. the unsubscribe ack that lands
                // during the Ctrl+C wait) are intentionally consumed
                // and dropped: they're request/response correlated,
                // not streaming events, and printing them as events
                // would corrupt the event stream a downstream
                // consumer is parsing.
            } else {
                // Unknown frame type. The wire protocol today is
                // closed-set Reply/Event/Error; a future server may
                // add a new type and we should not silently drop
                // it. Emit a one-line stderr diagnostic so the user
                // notices a CLI/server skew without breaking the
                // stdout event stream.
                err << "phosphorctl subscribe: unknown frame type '" << sanitiseForSingleLine(frameType)
                    << "' (CLI/server version skew?)\n";
                err.flush();
            }
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
        // Drain whatever the handler queued. The read end is
        // O_NONBLOCK (set in installSignalHandlers), so an empty
        // pipe surfaces as EAGAIN/EWOULDBLOCK rather than blocking
        // forever. Retry on EINTR (a syscall interrupted by a
        // signal during the drain is a transient condition); exit
        // on EAGAIN/EWOULDBLOCK (pipe empty), on EOF (n == 0,
        // shouldn't happen since this process holds the write end),
        // or on any other terminal error.
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
    // run the loop. We don't capture exec()'s return value — Qt6's
    // QCoreApplication::quit() is equivalent to exit(0), and no
    // path in this function calls exit(N) with N != 0, so the
    // return value is always 0 when the loop terminates normally.
    // The user-visible exit code is computed below from
    // serverDisconnected (set by the disconnected lambda).
    if (socket->state() != QLocalSocket::ConnectedState) {
        serverDisconnected = true;
    } else {
        QCoreApplication::exec();
    }
    // Server-initiated disconnect exits 2 so scripts driving us
    // can distinguish "Ctrl+C clean stop" from "server died".
    return serverDisconnected ? 2 : 0;
}

} // namespace Phosphorctl
