// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorIpc/IpcRouter.h>

#include "ipcrouterdetail.h"

#include <PhosphorIpc/IpcProtocol.h>
#include <PhosphorIpc/IpcSchemaGenerator.h>

#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QGenericArgument>
#include <QGenericReturnArgument>
#include <QJsonArray>
#include <QJsonValue>
#include <QLocalServer>
#include <QLocalSocket>
#include <QMetaMethod>
#include <QMetaObject>
#include <QMetaType>
#include <QProcessEnvironment>
#include <QString>
#include <QThread>
#include <QVariant>

#include <sys/stat.h> // umask

namespace PhosphorIpc {

namespace {

// Hard cap on a single NDJSON request line. The router buffers
// unframed bytes until canReadLine() returns true; without a cap,
// a peer that never sends '\n' would let the per-socket buffer
// grow until the process OOMs. 1 MiB is far above any realistic
// shell request (the upper-bound observed is ~few hundred bytes
// of args+target+fn payload) and well under any kernel send/recv
// buffer ceiling.
constexpr qint64 MaxLineBytes = 1024 * 1024;

// Cap on frames dispatched per readyRead() invocation. A peer that
// pipelines thousands of frames in a single TCP segment would
// otherwise let the dispatch loop monopolise the event loop and
// starve other connections. When the cap is hit and more bytes
// remain, the readyRead handler schedules itself again via a queued
// invocation, yielding to the event loop between batches.
constexpr int MaxFramesPerReadyRead = 64;

// Maximum number of CONSECUTIVE malformed frames a peer may send
// before the router force-closes the connection. The (N+1)-th
// consecutive malformed frame triggers the close (i.e. the cap is
// "the largest run we tolerate"). A well-formed frame anywhere in
// the stream resets the counter; we only close on sustained
// garbage, not on a one-off bad line.
constexpr int MaxConsecutiveMalformedFrames = 16;

// RAII scope for the ::umask process-global. We use ::umask to
// force a 0600 mode on the socket file at create time (atomic, no
// chmod-after-listen window). The umask is process-global and not
// thread-local, so a leak would taint any concurrent file
// creation; this guard ensures the restore happens even if a
// future change inserts a throwing call between the bind and the
// existing manual restore.
class UmaskScope
{
public:
    explicit UmaskScope(mode_t mask)
        : m_prev(::umask(mask))
    {
    }
    ~UmaskScope()
    {
        ::umask(m_prev);
    }
    UmaskScope(const UmaskScope&) = delete;
    UmaskScope& operator=(const UmaskScope&) = delete;
    UmaskScope(UmaskScope&&) = delete;
    UmaskScope& operator=(UmaskScope&&) = delete;

private:
    mode_t m_prev;
};

// Resolve the default socket path. $XDG_RUNTIME_DIR/phosphor.sock
// when XDG_RUNTIME_DIR is set, otherwise empty (caller logs and
// fails start()).
QString resolveDefaultSocketPath()
{
    const QString xdg = QProcessEnvironment::systemEnvironment().value(QStringLiteral("XDG_RUNTIME_DIR"));
    if (xdg.isEmpty()) {
        return {};
    }
    return QDir(xdg).filePath(QStringLiteral("phosphor.sock"));
}

} // namespace

IpcRouter::IpcRouter(QObject* parent)
    : QObject(parent)
{
}

IpcRouter::~IpcRouter()
{
    stop();
}

bool IpcRouter::start(const QString& socketPath)
{
    Q_ASSERT(thread() == QThread::currentThread());
    if (m_server) {
        qWarning("PhosphorIpc::IpcRouter::start: already started on '%s'", qPrintable(m_socketPath));
        return false;
    }
    const QString resolved = socketPath.isEmpty() ? resolveDefaultSocketPath() : socketPath;
    if (resolved.isEmpty()) {
        qWarning("PhosphorIpc::IpcRouter::start: no socket path (XDG_RUNTIME_DIR unset and no explicit path)");
        return false;
    }
    // Try to bind first. If listen() fails because a stale socket
    // file from a crashed previous run is sitting there, probe with
    // a quick connect, if nothing's listening, the path is dead
    // and we can safely remove + retry. If a live process IS
    // listening, the connect succeeds and we leave the path alone
    // (refuse to start). This avoids the "clobber a live listener
    // on the same path" bug a blind unlink would cause.
    m_server = std::make_unique<QLocalServer>(this);
    // We do NOT use setSocketOptions(UserAccessOption) here: that
    // option makes QLocalServer auto-unlink any existing socket
    // file before binding, which would silently clobber another
    // live listener on the same path. Instead we listen() against
    // the bare path with an umask scope that forces the
    // newly-created socket file to be mode 0600 atomically, no
    // chmod-after-listen window where the socket exists at the
    // umask-default mode. ::umask() is process-global (NOT
    // thread-local), so any other thread creating files during the
    // narrow scope below inherits the 0177 mask. The router
    // contract (GUI thread only, Q_ASSERT'd above) plus the
    // microsecond-scale scope makes the race window negligible in
    // practice; a future move to fchmod(socketDescriptor(), 0600)
    // after listen would remove the global mutation entirely.
    bool initialBound = false;
    {
        UmaskScope guard(0177);
        initialBound = m_server->listen(resolved);
    }
    if (!initialBound) {
        const QString initialError = m_server->errorString();
        bool removedStale = false;
        if (QFile::exists(resolved)) {
            // Probe via a fresh QLocalSocket: if a live router is
            // listening, the kernel-level connect handshake
            // completes near-instantly. If nothing's listening,
            // connect returns ECONNREFUSED and waitForConnected
            // reports false, only THEN do we unlink the stale
            // file and retry.
            QLocalSocket probe;
            probe.connectToServer(resolved);
            const bool aliveListener = probe.waitForConnected(200);
            probe.abort();
            if (!aliveListener) {
                QFile::remove(resolved);
                removedStale = true;
            }
        }
        bool retryBound = false;
        if (removedStale) {
            // Same umask scope as the initial listen so the
            // post-unlink retry also creates the socket file at
            // mode 0600 atomically.
            UmaskScope guard(0177);
            retryBound = m_server->listen(resolved);
        }
        if (!retryBound) {
            qWarning("PhosphorIpc::IpcRouter::start: listen() failed for '%s': %s", qPrintable(resolved),
                     qPrintable(removedStale ? m_server->errorString() : initialError));
            m_server.reset();
            return false;
        }
    }
    // Socket file is at mode 0600 by construction (the umask
    // scope above forced the create-time permissions); no
    // post-listen chmod needed. $XDG_RUNTIME_DIR is conventionally
    // 0700 on systemd systems (per the systemd-logind spec); a
    // non-systemd setup that points XDG_RUNTIME_DIR at /tmp would
    // not get that directory-level protection, which is why the
    // 0600 socket file mode is independent defense-in-depth.
    m_socketPath = resolved;
    QObject::connect(m_server.get(), &QLocalServer::newConnection, this, &IpcRouter::handleNewConnection);
    return true;
}

void IpcRouter::stop()
{
    Q_ASSERT(thread() == QThread::currentThread());
    // Clear subscription state BEFORE tearing down the server. The
    // server's reset destroys child sockets, which fire
    // disconnected() synchronously; that re-enters
    // handleClientDisconnected, which would otherwise mutate
    // m_subscriptionsBySocket while we're about to clear it. Clearing
    // first makes the disconnect-driven removes into safe no-ops
    // (the hash is empty already).
    m_subscriptionsBySocket.clear();
    m_malformedCountBySocket.clear();
    if (m_server) {
        m_server->close();
        m_server.reset();
    }
    m_socketPath.clear();
}

QString IpcRouter::socketPath() const
{
    return m_socketPath;
}

bool IpcRouter::registerTarget(const QString& name, QObject* object)
{
    Q_ASSERT(thread() == QThread::currentThread());
    if (name.isEmpty()) {
        qWarning("PhosphorIpc::IpcRouter::registerTarget: empty name ignored");
        return false;
    }
    if (!object) {
        qWarning("PhosphorIpc::IpcRouter::registerTarget: null object for '%s' ignored", qPrintable(name));
        return false;
    }
    if (m_targets.contains(name)) {
        QObject* existing = m_targets.value(name).data();
        if (existing == object) {
            // Same name + same object is idempotent; treat as success
            // so a caller calling registerTarget twice (or two
            // wrappers wrapping the same QObject) doesn't bounce.
            return true;
        }
        if (existing) {
            qWarning("PhosphorIpc::IpcRouter::registerTarget: duplicate target '%s' ignored", qPrintable(name));
            return false;
        }
        // Prior QObject was destroyed out from under us; the
        // QPointer auto-cleared without emitting targetUnregistered
        // (QPointer clearing is silent by design). We DON'T emit a
        // synthetic unregister here either, it would create an
        // observable empty-then-filled gap for any Qt::DirectConnection
        // slot reading listTargets() during the emission. Instead,
        // overwrite the slot in place and emit a single
        // targetRegistered. Observers that cared about the prior
        // QObject can connect to its destroyed() signal directly.
        m_targets.remove(name);
    }
    m_targets.insert(name, QPointer<QObject>(object));
    // Auto-remove the registry entry if the target QObject is
    // destroyed without an explicit unregisterTarget call (the
    // documented contract is "unregister first, then destroy", but
    // a contract violation shouldn't leak null-QPointer entries
    // forever). Context bound to `this` so the connection is torn
    // down when the router dies; lambda captures `name` by value
    // because the target QObject is being destroyed, so any
    // pointer-derived key would be stale by the time we ran.
    QObject::connect(object, &QObject::destroyed, this, [this, name]() {
        const auto it = m_targets.constFind(name);
        if (it != m_targets.constEnd() && !it.value()) {
            m_targets.erase(it);
            Q_EMIT targetUnregistered(name);
        }
    });
    Q_EMIT targetRegistered(name);
    return true;
}

void IpcRouter::unregisterTarget(const QString& name, QObject* object)
{
    Q_ASSERT(thread() == QThread::currentThread());
    const auto it = m_targets.constFind(name);
    if (it == m_targets.constEnd()) {
        return;
    }
    // Ownership check: if the caller passed a specific QObject,
    // only unregister when the registry still binds `name` to that
    // object. A target whose registerTarget was rejected (duplicate
    // name) MUST NOT be able to tear down the legitimate owner's
    // binding when it gets destroyed. Passing nullptr opts out of
    // the check (administrative unregister path).
    if (object && it.value().data() != object) {
        return;
    }
    m_targets.erase(it);
    Q_EMIT targetUnregistered(name);
}

QStringList IpcRouter::listTargets() const
{
    QStringList ids;
    ids.reserve(m_targets.size());
    // cbegin/cend (not begin/end) so the implicitly-shared QHash
    // doesn't detach on a const accessor in the hot path.
    for (auto it = m_targets.cbegin(); it != m_targets.cend(); ++it) {
        if (it.value()) {
            ids.append(it.key());
        }
    }
    return ids;
}

QObject* IpcRouter::target(const QString& name) const
{
    const auto it = m_targets.constFind(name);
    if (it == m_targets.constEnd()) {
        return nullptr;
    }
    return it.value().data();
}

QJsonObject IpcRouter::schemaFor(const QString& targetName) const
{
    // Delegate fully to the schema generator so router-direct callers
    // and the wire path agree on the unknown-target contract: an
    // unknown target returns the empty-shaped {target, functions:[],
    // signals:[]} document, not a bare {}. The wire dispatcher still
    // sends a NO_SUCH_TARGET error frame for `schema` requests on
    // unknown targets (see dispatch()); the convenience accessor
    // exposes the same shape callers would see by walking a real
    // registered target.
    return IpcSchemaGenerator::schemaFor(targetName, target(targetName));
}

QVariant IpcRouter::invoke(const QString& targetName, const QString& fn, const QVariantList& args,
                           InvokeOutcome* outcome, QString* errorMessage)
{
    Q_ASSERT(thread() == QThread::currentThread());
    auto fail = [&](InvokeOutcome code, const QString& msg) -> QVariant {
        if (outcome) {
            *outcome = code;
        }
        if (errorMessage) {
            *errorMessage = msg;
        }
        return {};
    };

    QObject* obj = target(targetName);
    if (!obj) {
        return fail(InvokeOutcome::NoSuchTarget, QStringLiteral("unknown target '%1'").arg(targetName));
    }
    // Pick the overload whose arity matches args.size(). When no
    // overload's arity lines up, findInvokableMethod still returns a
    // name-only fallback so we can surface a precise
    // ArgCountMismatch citing the actual expected arity.
    const QMetaMethod method = detail::findInvokableMethod(obj, fn, static_cast<int>(args.size()));
    if (!method.isValid()) {
        return fail(InvokeOutcome::NoSuchFn,
                    QStringLiteral("target '%1' has no invokable method '%2'").arg(targetName, fn));
    }
    if (method.parameterCount() != args.size()) {
        return fail(InvokeOutcome::ArgCountMismatch,
                    QStringLiteral("argument count mismatch: '%1.%2' expects %3, got %4")
                        .arg(targetName, fn)
                        .arg(method.parameterCount())
                        .arg(args.size()));
    }

    // Build the arg array. Qt's invokeMethod uses 10
    // QGenericArgument slots; QML-declared functions rarely exceed
    // this in practice. For each arg, coerce the incoming QVariant
    // to the method's expected parameter type.
    constexpr int MaxArgs = 10;
    if (args.size() > MaxArgs) {
        return fail(InvokeOutcome::ArgCountMismatch,
                    QStringLiteral("argument count exceeds dispatch limit of %1").arg(MaxArgs));
    }
    // reserve() is load-bearing: the second loop below builds
    // QGenericArgument values holding const T* pointers into each
    // coerced QVariant's internal storage. A reallocation during
    // append() would invalidate those pointers, so pre-reserve the
    // exact final size.
    QVariantList coerced;
    coerced.reserve(args.size());
    for (int i = 0; i < args.size(); ++i) {
        QVariant v = args.at(i);
        const QMetaType expected(method.parameterType(i));
        if (v.metaType() != expected) {
            if (!v.convert(expected)) {
                // typeName() can return nullptr for a default-
                // constructed QVariant (e.g. JSON null in an args
                // slot). QLatin1String(nullptr) is UB, so guard.
                const char* incomingType = args.at(i).typeName();
                return fail(InvokeOutcome::ArgConvertFailed,
                            QStringLiteral("arg %1 for '%2.%3': cannot convert %4 to %5")
                                .arg(i)
                                .arg(targetName, fn, QLatin1String(incomingType ? incomingType : "<null-variant>"),
                                     QLatin1String(expected.name())));
            }
        }
        coerced.append(std::move(v));
    }

    QGenericArgument g[MaxArgs];
    for (int i = 0; i < coerced.size(); ++i) {
        g[i] = QGenericArgument(coerced.at(i).typeName(), coerced.at(i).constData());
    }

    QVariant returnValue;
    const QMetaType returnType(method.returnMetaType());
    // Treat UnknownType the same as Void: a return type whose
    // metatype isn't registered can't be safely allocated via
    // QVariant(type, nullptr), so skip the return slot entirely
    // and let invoke() discard the value.
    if (returnType.isValid() && returnType.id() != QMetaType::Void && returnType.id() != QMetaType::UnknownType) {
        returnValue = QVariant(returnType, nullptr);
    }
    QGenericReturnArgument ret = returnValue.isValid()
        ? QGenericReturnArgument(returnValue.typeName(), returnValue.data())
        : QGenericReturnArgument();

    const bool ok =
        method.invoke(obj, Qt::DirectConnection, ret, g[0], g[1], g[2], g[3], g[4], g[5], g[6], g[7], g[8], g[9]);
    if (!ok) {
        return fail(
            InvokeOutcome::InvokeFailed,
            QStringLiteral("invocation of '%1.%2' failed (QMetaMethod::invoke returned false)").arg(targetName, fn));
    }
    if (outcome) {
        *outcome = InvokeOutcome::Ok;
    }
    return returnValue;
}

void IpcRouter::handleNewConnection()
{
    while (m_server && m_server->hasPendingConnections()) {
        QLocalSocket* socket = m_server->nextPendingConnection();
        if (!socket) {
            continue;
        }
        // Cap the per-socket kernel-side read buffer. Without this,
        // a client that opens the socket and never sends a newline
        // can pin arbitrary amounts of memory in the receive buffer
        // until canReadLine() finally returns true. handleClientReadyRead
        // also enforces the same cap at the userspace-buffer layer.
        socket->setReadBufferSize(MaxLineBytes);
        // Explicit Qt::DirectConnection: both signals fire from the
        // router's own thread (same thread as the QLocalServer), and
        // the dispatch / disconnect paths intentionally re-enter
        // router state synchronously. Spell it out so a future reader
        // doesn't assume queued semantics from AutoConnection.
        QObject::connect(
            socket, &QLocalSocket::readyRead, this,
            [this, socket]() {
                handleClientReadyRead(socket);
            },
            Qt::DirectConnection);
        QObject::connect(
            socket, &QLocalSocket::disconnected, this,
            [this, socket]() {
                handleClientDisconnected(socket);
            },
            Qt::DirectConnection);
    }
}

void IpcRouter::handleClientReadyRead(QLocalSocket* socket)
{
    if (!socket) {
        return;
    }
    // Hard cap on the unframed accumulation. canReadLine() only
    // returns true once a '\n' arrives; until then bytes accumulate
    // in the QLocalSocket's userspace buffer (bounded by
    // setReadBufferSize(MaxLineBytes) on accept; the kernel-side
    // receive buffer may transiently hold a bit more before Qt
    // notices). Once bytesAvailable reaches the cap without a
    // newline, no more bytes will be delivered to userspace and
    // there's no way for a valid line to arrive on this connection.
    // Force the socket closed with a MALFORMED_REQUEST diagnostic;
    // handleClientDisconnected will clean up the subscription state.
    if (!socket->canReadLine() && socket->bytesAvailable() >= MaxLineBytes) {
        closeWithMalformedDiagnostic(
            socket, QStringLiteral("request line exceeds %1 bytes; closing connection").arg(MaxLineBytes));
        return;
    }
    int frames = 0;
    while (socket->canReadLine()) {
        // A prior dispatch() in this readyRead burst may have force-
        // closed the socket via abort() (oversize line, malformed-frame
        // cap). canReadLine() can still return true off bytes that
        // were buffered before the close; stop iterating so we don't
        // keep parsing into a torn-down connection.
        if (socket->state() != QLocalSocket::ConnectedState) {
            return;
        }
        QByteArray line = socket->readLine();
        if (line.size() > MaxLineBytes) {
            // Defense-in-depth: readLine returned a line that's
            // somehow larger than the cap (shouldn't happen given the
            // pre-canReadLine guard above, but Qt's behavior here is
            // documented as "up to MaxBytes" without a hard floor).
            closeWithMalformedDiagnostic(
                socket, QStringLiteral("request line exceeds %1 bytes; closing connection").arg(MaxLineBytes));
            return;
        }
        // Strip the trailing newline; QJsonDocument tolerates it
        // but we keep parsing strict.
        while (!line.isEmpty() && (line.endsWith('\n') || line.endsWith('\r'))) {
            line.chop(1);
        }
        if (line.isEmpty()) {
            continue;
        }
        dispatch(socket, line);
        if (++frames >= MaxFramesPerReadyRead) {
            // Yield to the event loop so other connections (and the
            // event loop itself) aren't starved by a pipelining peer.
            // Reschedule ourselves only if data remains; readyRead
            // does NOT re-fire until new bytes arrive, so without
            // the queued kick the leftover frames would sit in the
            // buffer indefinitely.
            if (socket->canReadLine()) {
                QPointer<QLocalSocket> guarded(socket);
                QMetaObject::invokeMethod(
                    this,
                    [this, guarded]() {
                        if (guarded) {
                            handleClientReadyRead(guarded.data());
                        }
                    },
                    Qt::QueuedConnection);
            }
            return;
        }
    }
}

void IpcRouter::handleClientDisconnected(QLocalSocket* socket)
{
    if (!socket) {
        return;
    }
    // Drop the socket's subscription list, broadcastEvent skips
    // disconnected sockets anyway, but we explicitly free the
    // record so memory doesn't grow with churn. The malformed-frame
    // counter is the same shape; clear it too so the per-socket
    // map size stays bounded by live connections.
    m_subscriptionsBySocket.remove(socket);
    m_malformedCountBySocket.remove(socket);
    // Only schedule deleteLater for sockets the router still owns
    // as live children of m_server. When stop() resets m_server,
    // each child socket's destructor fires disconnected() synchronously,
    // and we'd be calling deleteLater() on a QObject already inside
    // its destruction path — Qt logs a warning ("cannot deleteLater()
    // an object already being destroyed"). The parent-child cleanup
    // covers that case for us.
    if (m_server && socket->parent() == m_server.get()) {
        socket->deleteLater();
    }
}

void IpcRouter::closeWithMalformedDiagnostic(QLocalSocket* socket, const QString& message)
{
    if (!socket) {
        return;
    }
    socket->write(writeLine(buildError(0, QString::fromUtf8(ErrorCode::MalformedRequest), message)));
    // waitForBytesWritten gives the diagnostic frame a chance to land
    // in the kernel send buffer before abort() resets the socket;
    // abort() discards any pending writes by design.
    socket->waitForBytesWritten(100);
    socket->abort();
}

void IpcRouter::dispatch(QLocalSocket* socket, const QByteArray& line)
{
    QString parseError;
    const auto reqOpt = parseRequest(line, &parseError);
    if (!reqOpt) {
        const QJsonObject err = buildError(0, QString::fromUtf8(ErrorCode::MalformedRequest), parseError);
        socket->write(writeLine(err));
        // Increment per-socket malformed-frame counter. If the peer
        // keeps streaming garbage past MaxConsecutiveMalformedFrames
        // in a row, close the connection so the router doesn't keep
        // echoing errors indefinitely.
        int& count = m_malformedCountBySocket[socket];
        if (++count > MaxConsecutiveMalformedFrames) {
            qWarning("PhosphorIpc::IpcRouter::dispatch: socket exceeded %d consecutive malformed frames; closing",
                     MaxConsecutiveMalformedFrames);
            // No additional diagnostic frame here — the per-frame
            // MALFORMED_REQUEST written via socket->write above
            // already told the peer; we only need to flush + abort.
            socket->waitForBytesWritten(100);
            socket->abort();
        }
        return;
    }
    // A well-formed frame clears the malformed counter; we only
    // close on CONSECUTIVE garbage, not on a one-off bad line in
    // an otherwise-healthy stream.
    m_malformedCountBySocket.remove(socket);
    const Request& req = *reqOpt;

    if (req.type == QLatin1String(RequestType::List)) {
        QJsonArray arr;
        for (const QString& name : listTargets()) {
            arr.append(name);
        }
        socket->write(writeLine(buildReply(req.id, arr)));
        return;
    }
    if (req.type == QLatin1String(RequestType::Schema)) {
        QObject* obj = target(req.target);
        if (!obj) {
            socket->write(writeLine(buildError(req.id, QString::fromUtf8(ErrorCode::NoSuchTarget),
                                               QStringLiteral("unknown target '%1'").arg(req.target))));
            return;
        }
        socket->write(writeLine(buildReply(req.id, IpcSchemaGenerator::schemaFor(req.target, obj))));
        return;
    }
    if (req.type == QLatin1String(RequestType::Call)) {
        InvokeOutcome outcome = InvokeOutcome::Ok;
        QString invokeError;
        const QVariant result = invoke(req.target, req.fn, req.args, &outcome, &invokeError);
        if (outcome != InvokeOutcome::Ok) {
            // Structured outcome -> wire error code. No string
            // matching: a future tweak to the diagnostic message
            // can't silently misclassify the error.
            QString code = QString::fromUtf8(ErrorCode::InvocationFailed);
            switch (outcome) {
            case InvokeOutcome::NoSuchTarget:
                code = QString::fromUtf8(ErrorCode::NoSuchTarget);
                break;
            case InvokeOutcome::NoSuchFn:
                code = QString::fromUtf8(ErrorCode::NoSuchFn);
                break;
            case InvokeOutcome::ArgCountMismatch:
            case InvokeOutcome::ArgConvertFailed:
                code = QString::fromUtf8(ErrorCode::InvalidArg);
                break;
            case InvokeOutcome::InvokeFailed:
                code = QString::fromUtf8(ErrorCode::InvocationFailed);
                break;
            case InvokeOutcome::Ok:
                Q_UNREACHABLE();
            }
            socket->write(writeLine(buildError(req.id, code, invokeError)));
            return;
        }
        socket->write(writeLine(buildReply(req.id, variantToJson(result))));
        return;
    }
    if (req.type == QLatin1String(RequestType::Subscribe)) {
        handleSubscribe(socket, req.id, req.target, req.signalName);
        return;
    }
    if (req.type == QLatin1String(RequestType::Unsubscribe)) {
        handleUnsubscribe(socket, req.id, req.subscriptionId);
        return;
    }
    socket->write(writeLine(buildError(req.id, QString::fromUtf8(ErrorCode::MalformedRequest),
                                       QStringLiteral("unknown request type '%1'").arg(req.type))));
}

} // namespace PhosphorIpc
