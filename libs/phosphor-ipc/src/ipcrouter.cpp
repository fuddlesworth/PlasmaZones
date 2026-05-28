// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorIpc/IpcRouter.h>

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

// Per-socket malformed-frame cap. Without this a peer can send an
// unbounded sequence of garbage lines: each one parses, fails,
// gets a MALFORMED_REQUEST error frame written back, and the
// connection stays open. Closing the connection after a small run
// of consecutive parse failures bounds the DoS surface.
constexpr int MaxMalformedFrames = 16;

// Per-socket subscription cap. Without this a client can subscribe
// to the same (target, signal) thousands of times and amplify each
// event into thousands of write attempts, multiplying broadcast
// cost and write-buffer pressure.
constexpr int MaxSubscriptionsPerSocket = 256;

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

// First method index above QObject's built-ins. Used as the lower
// bound for findInvokableMethod / findSignal so QObject's own
// destroyed / objectNameChanged / deleteLater don't leak onto the
// wire as callable / subscribable surface, while every method
// declared on IpcTarget or any user-defined subclass stays in
// scope (including methods inherited from a non-QObject base
// class, those start at QObject's methodCount, not at the leaf's
// methodOffset). Keeps findInvokableMethod, findSignal, and the
// schema generator in lockstep on the same visibility boundary.
[[nodiscard]] int qobjectMethodCount()
{
    return QObject::staticMetaObject.methodCount();
}

// Find a Q_INVOKABLE method by name AND arity. When the target has
// overloaded methods (same name, different arities) the wire dispatch
// needs to pick the overload whose parameterCount matches the
// caller's args.size(); picking the first match by name alone would
// surface ArgCountMismatch even though a matching overload exists.
// Walks the full inheritance chain above QObject's built-ins so a
// method declared on a base class above QObject is still invocable.
// Filters to Public access only: protected Q_INVOKABLE methods are
// a "subclass-only" marker by convention; exposing them on the wire
// would defeat that.
QMetaMethod findInvokableMethod(const QMetaObject* meta, const QString& fnName, int expectedArity)
{
    if (!meta) {
        return {};
    }
    const QByteArray needle = fnName.toUtf8();
    QMetaMethod nameOnlyFallback;
    for (int i = qobjectMethodCount(); i < meta->methodCount(); ++i) {
        const QMetaMethod m = meta->method(i);
        if (m.methodType() == QMetaMethod::Signal) {
            continue;
        }
        if (m.access() != QMetaMethod::Public) {
            continue;
        }
        if (m.name() != needle) {
            continue;
        }
        if (m.parameterCount() == expectedArity) {
            return m;
        }
        // Stash the first non-matching-arity overload so the caller
        // can still surface a precise ArgCountMismatch with the
        // metamethod's expected parameter count when no overload's
        // arity lines up.
        if (!nameOnlyFallback.isValid()) {
            nameOnlyFallback = m;
        }
    }
    return nameOnlyFallback;
}

// Find a signal by name. Same shape as findInvokableMethod, but
// filters to signals only. The subscribe path uses this for
// existence validation (typo'd signal names return NO_SUCH_SIGNAL
// at subscribe time instead of silently never delivering events).
// Walks the full chain above QObject's built-ins so signals
// declared on a base class are still resolvable while QObject's
// destroyed / objectNameChanged remain filtered out. Filters to
// Public-access signals only; protected/private signal access keeps
// them out of the wire subscribe surface.
QMetaMethod findSignal(const QMetaObject* meta, const QString& signalName)
{
    if (!meta) {
        return {};
    }
    const QByteArray needle = signalName.toUtf8();
    for (int i = qobjectMethodCount(); i < meta->methodCount(); ++i) {
        const QMetaMethod m = meta->method(i);
        if (m.methodType() != QMetaMethod::Signal) {
            continue;
        }
        if (m.access() != QMetaMethod::Public) {
            continue;
        }
        if (m.name() == needle) {
            return m;
        }
    }
    return {};
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
    const mode_t prevUmask = ::umask(0177);
    const bool initialBound = m_server->listen(resolved);
    ::umask(prevUmask);
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
            const mode_t prevUmaskRetry = ::umask(0177);
            retryBound = m_server->listen(resolved);
            ::umask(prevUmaskRetry);
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
    const QMetaMethod method = findInvokableMethod(obj->metaObject(), fn, static_cast<int>(args.size()));
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
        QObject::connect(socket, &QLocalSocket::readyRead, this, [this, socket]() {
            handleClientReadyRead(socket);
        });
        QObject::connect(socket, &QLocalSocket::disconnected, this, [this, socket]() {
            handleClientDisconnected(socket);
        });
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
        socket->write(writeLine(
            buildError(0, QString::fromUtf8(ErrorCode::MalformedRequest),
                       QStringLiteral("request line exceeds %1 bytes; closing connection").arg(MaxLineBytes))));
        // waitForBytesWritten gives the diagnostic frame a chance to
        // land in the kernel send buffer before abort() resets the
        // socket; abort() discards any pending writes by design.
        socket->waitForBytesWritten(100);
        socket->abort();
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
            socket->write(writeLine(
                buildError(0, QString::fromUtf8(ErrorCode::MalformedRequest),
                           QStringLiteral("request line exceeds %1 bytes; closing connection").arg(MaxLineBytes))));
            socket->waitForBytesWritten(100);
            socket->abort();
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
    socket->deleteLater();
}

void IpcRouter::dispatch(QLocalSocket* socket, const QByteArray& line)
{
    QString parseError;
    const auto reqOpt = parseRequest(line, &parseError);
    if (!reqOpt) {
        const QJsonObject err = buildError(0, QString::fromUtf8(ErrorCode::MalformedRequest), parseError);
        socket->write(writeLine(err));
        // Increment per-socket malformed-frame counter. If the peer
        // keeps streaming garbage past MaxMalformedFrames in a row,
        // close the connection so the router doesn't keep echoing
        // errors indefinitely.
        int& count = m_malformedCountBySocket[socket];
        if (++count > MaxMalformedFrames) {
            qWarning("PhosphorIpc::IpcRouter::dispatch: socket exceeded %d malformed frames; closing",
                     MaxMalformedFrames);
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

void IpcRouter::handleSubscribe(QLocalSocket* socket, qint64 id, const QString& targetName, const QString& signalName)
{
    if (targetName.isEmpty() || signalName.isEmpty()) {
        socket->write(writeLine(buildError(id, QString::fromUtf8(ErrorCode::MalformedRequest),
                                           QStringLiteral("subscribe requires non-empty target and signal"))));
        return;
    }
    QObject* obj = target(targetName);
    if (!obj) {
        socket->write(writeLine(buildError(id, QString::fromUtf8(ErrorCode::NoSuchTarget),
                                           QStringLiteral("unknown target '%1'").arg(targetName))));
        return;
    }
    // Validate the signal name against the target's metaobject so
    // typos surface as NO_SUCH_SIGNAL instead of silently
    // accepting a subscription that will never fire. Signal lookup
    // here is advisory: broadcastEvent dispatches by string name,
    // not by metaobject index, so a future signal added via QML
    // dynamic property would also work even without recompiling.
    if (!findSignal(obj->metaObject(), signalName).isValid()) {
        socket->write(
            writeLine(buildError(id, QString::fromUtf8(ErrorCode::NoSuchSignal),
                                 QStringLiteral("target '%1' has no signal '%2'").arg(targetName, signalName))));
        return;
    }

    // Per-socket cap on subscriptions. A misbehaving client can't
    // pin amplification factor on broadcastEvent by stacking
    // thousands of subscriptions; reject past the cap with a
    // MALFORMED_REQUEST (the closest existing code that fits "your
    // request was structurally fine but exceeded a server-side
    // resource limit"; adding a dedicated code would be a wire
    // break in this protocol version).
    QList<Subscription>& subs = m_subscriptionsBySocket[socket];
    if (subs.size() >= MaxSubscriptionsPerSocket) {
        socket->write(writeLine(
            buildError(id, QString::fromUtf8(ErrorCode::MalformedRequest),
                       QStringLiteral("subscription cap of %1 per socket exceeded").arg(MaxSubscriptionsPerSocket))));
        return;
    }
    // Reject duplicate (target, signal) subscriptions on the same
    // socket. Each subscribe call gets a distinct subscriptionId
    // (the request id), so without this guard a client that calls
    // subscribe N times for the same signal receives N copies of
    // every event and inflates the write-side cost N-fold.
    for (const Subscription& existing : subs) {
        if (existing.target == targetName && existing.signalName == signalName) {
            socket->write(writeLine(
                buildError(id, QString::fromUtf8(ErrorCode::MalformedRequest),
                           QStringLiteral("already subscribed to '%1.%2' on this connection (subscriptionId %3)")
                               .arg(targetName, signalName)
                               .arg(existing.subscriptionId))));
            return;
        }
    }
    // Subscription id == request id; clients use that on subsequent
    // unsubscribe and to demultiplex inbound events. The router
    // doesn't enforce uniqueness across all clients, id is scoped
    // per-socket because the wire identity is (socket, subscriptionId).
    Subscription sub;
    sub.socket = socket;
    sub.subscriptionId = id;
    sub.target = targetName;
    sub.signalName = signalName;
    subs.append(sub);

    // Acknowledge the subscription is live so the client knows to
    // start streaming events.
    socket->write(writeLine(buildReply(id, QJsonValue::Null)));
}

void IpcRouter::handleUnsubscribe(QLocalSocket* socket, qint64 id, qint64 subscriptionId)
{
    auto it = m_subscriptionsBySocket.find(socket);
    if (it == m_subscriptionsBySocket.end()) {
        socket->write(writeLine(buildError(id, QString::fromUtf8(ErrorCode::NoSuchSubscription),
                                           QStringLiteral("no subscriptions on this connection"))));
        return;
    }
    QList<Subscription>& subs = it.value();
    for (int i = 0; i < subs.size(); ++i) {
        if (subs.at(i).subscriptionId == subscriptionId) {
            subs.removeAt(i);
            socket->write(writeLine(buildReply(id, QJsonValue::Null)));
            return;
        }
    }
    socket->write(writeLine(buildError(id, QString::fromUtf8(ErrorCode::NoSuchSubscription),
                                       QStringLiteral("unknown subscriptionId %1").arg(subscriptionId))));
}

void IpcRouter::broadcastEvent(const QString& targetName, const QString& signalName, const QJsonArray& args)
{
    Q_ASSERT(thread() == QThread::currentThread());
    // Snapshot the matching (socket, subscriptionId) pairs BEFORE
    // any write/flush. QLocalSocket::write or flush can synchronously
    // emit errorOccurred / disconnected (peer closed the socket
    // between accept and send, for instance); the disconnected signal
    // re-enters handleClientDisconnected which removes the matching
    // entry from m_subscriptionsBySocket. Iterating the hash while
    // that mutation runs would invalidate our iterator and either
    // crash or skip live subscribers. Materialising the dispatch
    // list first lets the send loop tolerate concurrent prunes.
    //
    // Iteration is O(N subscriptions); for the expected per-process
    // subscription count (≤ ~50 in any realistic shell deployment)
    // the linear scan is cheaper than maintaining a secondary index.
    struct PendingSend
    {
        QPointer<QLocalSocket> socket;
        qint64 subscriptionId;
    };
    QList<PendingSend> pending;
    for (auto it = m_subscriptionsBySocket.cbegin(); it != m_subscriptionsBySocket.cend(); ++it) {
        QLocalSocket* sock = it.key();
        if (!sock || sock->state() != QLocalSocket::ConnectedState) {
            continue;
        }
        for (const Subscription& sub : it.value()) {
            if (sub.target == targetName && sub.signalName == signalName) {
                pending.append({QPointer<QLocalSocket>(sock), sub.subscriptionId});
            }
        }
    }

    // Write-side cap, symmetric to the read-side MaxLineBytes guard
    // in handleClientReadyRead. A subscriber that never read from its
    // socket would otherwise let the per-socket pending-write queue
    // grow unbounded as broadcast events pile up. 16 MiB is the cap
    // (16x the read cap because event payloads can legitimately be
    // larger than request lines, and a slow consumer is more common
    // than a malformed client). When exceeded, force-close the
    // subscriber. handleClientDisconnected prunes its subscription
    // entries.
    constexpr qint64 MaxBytesToWrite = 16 * 1024 * 1024;
    for (const PendingSend& p : pending) {
        QLocalSocket* sock = p.socket.data();
        // Socket may have disconnected mid-broadcast (a prior send's
        // flush in this loop fired a chained disconnected; the
        // QPointer auto-cleared). Skip without trying to write.
        if (!sock || sock->state() != QLocalSocket::ConnectedState) {
            continue;
        }
        if (sock->bytesToWrite() > MaxBytesToWrite) {
            qWarning(
                "PhosphorIpc::IpcRouter::broadcastEvent: subscriber on '%s/%s' is %lld bytes behind; "
                "closing the connection",
                qPrintable(targetName), qPrintable(signalName), static_cast<long long>(sock->bytesToWrite()));
            sock->abort();
            continue;
        }
        // Write without per-send flush(). QLocalSocket::flush() can
        // synchronously emit disconnected() and errorOccurred(), which
        // re-enter handleClientDisconnected and mutate
        // m_subscriptionsBySocket while broadcastEvent's outer loop is
        // still iterating; even with the pending-list snapshot, a
        // re-entrant broadcastEvent (e.g. a target slot wired to a
        // property change reached via the synchronous error/disconnect
        // emit) would observe inconsistent state. Without flush(), Qt
        // drains the writes on the next event-loop return; ordering is
        // preserved (QLocalSocket is FIFO per socket) and the latency
        // cost is one event-loop iteration per burst, acceptable for
        // wire-event delivery in a UI shell.
        sock->write(writeLine(buildEvent(p.subscriptionId, args)));
    }
}

} // namespace PhosphorIpc
