// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorIpc/IpcRouter.h>

#include "ipcrouterdetail.h"

#include <PhosphorIpc/IpcCallContext.h>
#include <PhosphorIpc/IpcPendingReply.h>
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

#include <algorithm>
#include <atomic>

#include <sys/stat.h> // umask, lstat
#include <unistd.h> // getuid

namespace PhosphorIpc {

namespace {

// Whether @p path is a socket that a crashed router of this user left:
// a socket file of this user that refuses a connection. Only a refusal
// proves that nothing listens. A listener whose backlog is full (its
// process busy) does not answer the probe in time and reports no
// error, and a file that is not a socket also refuses: neither may be
// removed.
bool isStaleSocket(const QString& path)
{
    struct stat info{};
    if (::lstat(QFile::encodeName(path).constData(), &info) != 0 || !S_ISSOCK(info.st_mode)
        || info.st_uid != ::getuid()) {
        return false;
    }
    QLocalSocket probe;
    probe.connectToServer(path);
    if (probe.waitForConnected(200)) {
        probe.abort();
        return false;
    }
    return probe.error() == QLocalSocket::ConnectionRefusedError;
}

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

// The id of a new client connection. One counter for the process, so that
// an id is never reused, not even by another router (which may live on
// another thread, hence the atomic). 0 is never handed out: it means "no
// connection".
quint64 nextConnectionId()
{
    static std::atomic<quint64> last{0};
    return ++last;
}

} // namespace

IpcRouter::IpcRouter(QObject* parent)
    : QObject(parent)
{
}

IpcRouter::~IpcRouter()
{
    stop();
    // The event loop will not run for this router again: tell about the
    // connections stop() closed (and any closed before it that the loop
    // has not got to) now.
    announceClosedConnections();
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
    // file from a crashed previous run is sitting there, remove it
    // and retry. A path is stale only when isStaleSocket() says so:
    // a live listener, also a busy one, and a file that is not a
    // socket keep the path, and start() fails. This avoids the
    // "clobber a live listener on the same path" bug a blind unlink
    // would cause.
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
    // contract (owning thread only, Q_ASSERT'd above) plus the
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
        if (isStaleSocket(resolved)) {
            QFile::remove(resolved);
            removedStale = true;
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
    // Forget every connection first, then close the sockets. The closes
    // are booked here, once, after the sockets are closed.
    QList<quint64> closed = m_socketByConnectionId.keys();
    std::sort(closed.begin(), closed.end());
    m_subscriptionsBySocket.clear();
    m_malformedCountBySocket.clear();
    m_connectionIdBySocket.clear();
    m_socketByConnectionId.clear();
    m_pendingCalls.clear();
    m_pendingTokensByConnection.clear();
    if (m_server) {
        // The server deletes its child sockets when it goes, at once. A
        // target method may run stop() while a dispatch on one of them is
        // on the stack, so the router takes the sockets over, closes them
        // and deletes them through retireSocket, after any such dispatch
        // unwinds. Their handlers go first: the bookkeeping that
        // handleClientDisconnected would do is done above.
        const QList<QLocalSocket*> sockets = m_server->findChildren<QLocalSocket*>(Qt::FindDirectChildrenOnly);
        for (QLocalSocket* socket : sockets) {
            QObject::disconnect(socket, nullptr, this, nullptr);
            socket->setParent(this);
            socket->abort();
            retireSocket(socket);
        }
        m_server->close();
        m_server.reset();
    }
    m_socketPath.clear();
    for (const quint64 id : std::as_const(closed)) {
        connectionGone(id);
    }
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
        // QObject can connect to its destroyed() signal directly. The
        // prior object's subscriptions end, as unregisterTarget ends
        // them, so they do not stream the new object's events.
        m_targets.remove(name);
        endSubscriptionsTo(name);
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
            endSubscriptionsTo(name);
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
    endSubscriptionsTo(name);
    Q_EMIT targetUnregistered(name);
}

void IpcRouter::endSubscriptionsTo(const QString& name)
{
    // Subscriptions go with their target. Kept, they would stream again
    // when the name is registered later, perhaps by another object, to a
    // client that subscribed to the old one. Each subscriber hears that its
    // stream ended: an error frame whose id is the subscription id. The
    // writes are buffered and cannot close a socket here, so the hash is
    // not changed under the loop.
    for (auto subs = m_subscriptionsBySocket.begin(); subs != m_subscriptionsBySocket.end(); ++subs) {
        QLocalSocket* socket = subs.key();
        QList<Subscription>& list = subs.value();
        for (auto sub = list.begin(); sub != list.end();) {
            if (sub->target != name) {
                ++sub;
                continue;
            }
            if (socket && socket->state() == QLocalSocket::ConnectedState) {
                socket->write(writeLine(buildError(sub->subscriptionId, QString::fromUtf8(ErrorCode::NoSuchTarget),
                                                   QStringLiteral("target '%1' was unregistered").arg(name))));
            }
            sub = list.erase(sub);
        }
    }
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
    return invokeCall(targetName, fn, args, 0, 0, outcome, errorMessage);
}

QVariant IpcRouter::invokeCall(const QString& targetName, const QString& fn, const QVariantList& args,
                               quint64 connectionId, qint64 requestId, InvokeOutcome* outcome, QString* errorMessage)
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
    // A QVariant parameter (an untyped QML function argument) takes the
    // incoming variant as it is: QVariant::convert() cannot convert to
    // QVariant itself, and the method wants the variant, not its content.
    const QMetaType variantType = QMetaType::fromType<QVariant>();
    QVariantList coerced;
    coerced.reserve(args.size());
    for (int i = 0; i < args.size(); ++i) {
        QVariant v = args.at(i);
        const QMetaType expected(method.parameterType(i));
        if (expected != variantType && v.metaType() != expected) {
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
        g[i] = QMetaType(method.parameterType(i)) == variantType
            ? QGenericArgument("QVariant", &coerced.at(i))
            : QGenericArgument(coerced.at(i).typeName(), coerced.at(i).constData());
    }

    QVariant returnValue;
    const QMetaType returnType(method.returnMetaType());
    // Treat UnknownType the same as Void: a return type whose
    // metatype isn't registered can't be safely allocated via
    // QVariant(type, nullptr), so skip the return slot entirely
    // and let invoke() discard the value.
    // A QVariant return (an untyped QML function) writes into returnValue
    // itself, not into a variant held inside it.
    const bool variantReturn = returnType == variantType;
    if (!variantReturn && returnType.isValid() && returnType.id() != QMetaType::Void
        && returnType.id() != QMetaType::UnknownType) {
        returnValue = QVariant(returnType, nullptr);
    }
    QGenericReturnArgument ret = variantReturn ? QGenericReturnArgument("QVariant", &returnValue)
        : returnValue.isValid()                ? QGenericReturnArgument(returnValue.typeName(), returnValue.data())
                                               : QGenericReturnArgument();

    // The method runs as a wire call on its object, so that a target with an
    // IpcCallContext can fail it. The record is on this stack, not in the
    // target, which the method may delete. The outer call comes back for a
    // method that spins a nested event loop in which another call runs.
    IpcCallContext::Call call;
    call.object = dynamic_cast<const void*>(obj);
    call.outer = IpcCallContext::current();
    call.loopLevel = QThread::currentThread()->loopLevel();
    call.router = this;
    call.connectionId = connectionId;
    call.requestId = requestId;
    IpcCallContext::current() = &call;
    const bool ok =
        method.invoke(obj, Qt::DirectConnection, ret, g[0], g[1], g[2], g[3], g[4], g[5], g[6], g[7], g[8], g[9]);
    IpcCallContext::current() = call.outer;
    if (!ok) {
        // QMetaMethod::invoke fails before it runs a moc-generated method,
        // so no pending reply exists then. A hand-written qt_metacall may
        // run the method and still report failure: the error below then
        // answers the call, and the pending reply has nothing left to
        // answer.
        if (call.pendingToken != 0) {
            takePendingCall(call.pendingToken);
        }
        return fail(
            InvokeOutcome::InvokeFailed,
            QStringLiteral("invocation of '%1.%2' failed (QMetaMethod::invoke returned false)").arg(targetName, fn));
    }
    if (call.deferred) {
        // The call has one answer, the first one given. A method that
        // deferred and then failed the call is answered by the error now,
        // unless its pending reply answered (or abandoned) the call during
        // the method already. Taking the call out of m_pendingCalls makes
        // a later finish() on the pending reply a no-op.
        if (call.failed && (call.pendingToken == 0 || takePendingCall(call.pendingToken))) {
            return fail(InvokeOutcome::CallFailed, call.message);
        }
        if (outcome) {
            *outcome = InvokeOutcome::Deferred;
        }
        return {};
    }
    if (call.failed) {
        return fail(InvokeOutcome::CallFailed, call.message);
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
        // until canReadLine() finally returns true. readFrames also
        // enforces the same cap at the userspace-buffer layer.
        socket->setReadBufferSize(detail::MaxLineBytes);
        const quint64 connectionId = nextConnectionId();
        m_connectionIdBySocket.insert(socket, connectionId);
        m_socketByConnectionId.insert(connectionId, socket);
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
    // A dispatch on this socket is on the stack already when a target
    // method spins a nested event loop and the queued batch kick below
    // runs inside it (Qt does not re-emit readyRead for a socket whose
    // readyRead is still being handled). Reading on would answer later
    // requests before the running one; the outer readFrames reads them
    // when the method returns.
    if (!socket || m_socketsInDispatch.contains(socket)) {
        return;
    }
    m_socketsInDispatch.insert(socket);
    readFrames(socket);
    m_socketsInDispatch.remove(socket);
    if (m_socketsToDelete.remove(socket)) {
        socket->deleteLater();
    }
}

void IpcRouter::readFrames(QLocalSocket* socket)
{
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
    // Checked again when the lines run out: the bytes may have come in
    // while a dispatch below ran a nested event loop, whose readyRead
    // brought no new pass here, and with the buffer full none comes.
    const auto closeIfLineTooLong = [this, socket]() {
        if (socket->state() == QLocalSocket::ConnectedState && !socket->canReadLine()
            && socket->bytesAvailable() >= detail::MaxLineBytes) {
            closeWithMalformedDiagnostic(
                socket, QStringLiteral("request line exceeds %1 bytes; closing connection").arg(detail::MaxLineBytes));
        }
    };
    closeIfLineTooLong();
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
        if (line.size() > detail::MaxLineBytes) {
            // Defense-in-depth: readLine returned a line that's
            // somehow larger than the cap (shouldn't happen given the
            // pre-canReadLine guard above, but Qt's behavior here is
            // documented as "up to MaxBytes" without a hard floor).
            closeWithMalformedDiagnostic(
                socket, QStringLiteral("request line exceeds %1 bytes; closing connection").arg(detail::MaxLineBytes));
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
        if (++frames >= detail::MaxFramesPerReadyRead) {
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
    closeIfLineTooLong();
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
    // The connection's id goes, so sendEvent and pending replies find it
    // closed, and so do its deferred calls, whose answers have nowhere to
    // go. stop() forgets its sockets and books their closes itself.
    const quint64 connectionId = m_connectionIdBySocket.take(socket);
    if (connectionId != 0) {
        m_socketByConnectionId.remove(connectionId);
        const QSet<quint64> tokens = m_pendingTokensByConnection.take(connectionId);
        for (const quint64 token : tokens) {
            m_pendingCalls.remove(token);
        }
        connectionGone(connectionId);
    }
    retireSocket(socket);
}

void IpcRouter::retireSocket(QLocalSocket* socket)
{
    // A deleteLater() made inside a nested event loop runs in that loop,
    // so a socket closed while a target method spins one would be freed
    // under the dispatch that runs the method. handleClientReadyRead
    // deletes such a socket when the dispatch unwinds.
    if (m_socketsInDispatch.contains(socket)) {
        m_socketsToDelete.insert(socket);
        return;
    }
    socket->deleteLater();
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
        // keeps streaming garbage past detail::MaxConsecutiveMalformedFrames
        // in a row, close the connection so the router doesn't keep
        // echoing errors indefinitely.
        int& count = m_malformedCountBySocket[socket];
        if (++count > detail::MaxConsecutiveMalformedFrames) {
            qWarning("PhosphorIpc::IpcRouter::dispatch: socket exceeded %d consecutive malformed frames; closing",
                     detail::MaxConsecutiveMalformedFrames);
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
        // readFrames dispatches only for an open socket, and the router
        // knows the id of every one.
        const quint64 connectionId = m_connectionIdBySocket.value(socket);
        Q_ASSERT(connectionId != 0);
        InvokeOutcome outcome = InvokeOutcome::Ok;
        QString invokeError;
        const QVariant result = invokeCall(req.target, req.fn, req.args, connectionId, req.id, &outcome, &invokeError);
        // The method may have closed the connection (stop(), or sendEvent
        // past the output limit). Its answer has nowhere to go.
        if (socket->state() != QLocalSocket::ConnectedState) {
            return;
        }
        if (outcome == InvokeOutcome::Deferred) {
            if (connectionId == 0) {
                // A deferral needs the connection's id, and without one
                // the pending reply held no call: nothing would answer.
                socket->write(writeLine(buildError(req.id, QString::fromUtf8(ErrorCode::CallAbandoned),
                                                   QStringLiteral("the call came from an unknown connection"))));
            }
            // Otherwise the pending reply answers the call later, or did
            // already.
            return;
        }
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
            case InvokeOutcome::CallFailed:
                code = QString::fromUtf8(ErrorCode::CallFailed);
                break;
            case InvokeOutcome::Ok:
            case InvokeOutcome::Deferred:
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

IpcPendingReply IpcRouter::deferCall(IpcCallContext::Call& call)
{
    Q_ASSERT(thread() == QThread::currentThread());
    // The connection may have closed during the method already (stop(),
    // sendEvent past the output limit). Its calls were taken out of the
    // books then, and an entry made now would stay until the target drops
    // the pending reply. The pending reply holds the call all the same,
    // and finds nothing to answer.
    if (!connectedSocket(call.connectionId)) {
        call.pendingToken = ++m_lastPendingToken;
        return IpcPendingReply(this, call.pendingToken, call.connectionId);
    }
    QSet<quint64>& tokens = m_pendingTokensByConnection[call.connectionId];
    if (tokens.size() >= MaxPendingCallsPerConnection) {
        // An error the method sent already is the more telling one.
        if (!call.failed) {
            call.failed = true;
            call.message = QStringLiteral("too many deferred calls waiting on this connection (cap %1)")
                               .arg(MaxPendingCallsPerConnection);
        }
        return {};
    }
    const quint64 token = ++m_lastPendingToken;
    m_pendingCalls.insert(token, PendingCall{call.connectionId, call.requestId});
    tokens.insert(token);
    call.pendingToken = token;
    return IpcPendingReply(this, token, call.connectionId);
}

std::optional<IpcRouter::PendingCall> IpcRouter::takePendingCall(quint64 token)
{
    const auto it = m_pendingCalls.constFind(token);
    if (it == m_pendingCalls.cend()) {
        return std::nullopt;
    }
    const PendingCall pending = it.value();
    m_pendingCalls.erase(it);
    const auto tokens = m_pendingTokensByConnection.find(pending.connectionId);
    if (tokens != m_pendingTokensByConnection.end()) {
        tokens->remove(token);
        if (tokens->isEmpty()) {
            m_pendingTokensByConnection.erase(tokens);
        }
    }
    return pending;
}

bool IpcRouter::finishPendingCall(quint64 token, const QJsonValue& result)
{
    Q_ASSERT(thread() == QThread::currentThread());
    const std::optional<PendingCall> pending = takePendingCall(token);
    QLocalSocket* socket = pending ? connectedSocket(pending->connectionId) : nullptr;
    if (!socket) {
        return false;
    }
    socket->write(writeLine(buildReply(pending->requestId, result)));
    return true;
}

bool IpcRouter::failPendingCall(quint64 token, const QString& code, const QString& message)
{
    Q_ASSERT(thread() == QThread::currentThread());
    const std::optional<PendingCall> pending = takePendingCall(token);
    QLocalSocket* socket = pending ? connectedSocket(pending->connectionId) : nullptr;
    if (!socket) {
        return false;
    }
    socket->write(writeLine(buildError(pending->requestId, code, message)));
    return true;
}

QLocalSocket* IpcRouter::connectedSocket(quint64 connectionId) const
{
    QLocalSocket* socket = m_socketByConnectionId.value(connectionId).data();
    return socket && socket->state() == QLocalSocket::ConnectedState ? socket : nullptr;
}

void IpcRouter::connectionGone(quint64 connectionId)
{
    // connectionClosed goes out from the event loop. A close happens
    // inside router calls (the abort in sendEvent or broadcastEvent when a
    // client is too far behind, stop()), and a slot that ends the
    // connection's work must not run inside the target code that made
    // that call. One queued flush serves every close
    // booked before it runs.
    m_closedConnections.append(connectionId);
    if (m_closedConnections.size() == 1) {
        QMetaObject::invokeMethod(this, &IpcRouter::announceClosedConnections, Qt::QueuedConnection);
    }
}

void IpcRouter::announceClosedConnections()
{
    // takeFirst, not a range loop: a slot may close another connection,
    // which appends to the list while it is being sent. A slot that
    // destroys the router (against the IpcRouter.h lifetime rule, but
    // cheap to survive) ends the loop.
    const QPointer<IpcRouter> self(this);
    while (self && !m_closedConnections.isEmpty()) {
        Q_EMIT connectionClosed(m_closedConnections.takeFirst());
    }
}

} // namespace PhosphorIpc
