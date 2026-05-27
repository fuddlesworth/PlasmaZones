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
#include <QVariant>

namespace PhosphorIpc {

namespace {

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

// Find a Q_INVOKABLE method by name. Returns an invalid QMetaMethod
// if none match. Doesn't disambiguate by arity — the invoke path
// matches arg count against the metamethod's parameterCount.
QMetaMethod findInvokableMethod(const QMetaObject* meta, const QString& fnName)
{
    if (!meta) {
        return {};
    }
    const QByteArray needle = fnName.toUtf8();
    for (int i = meta->methodOffset(); i < meta->methodCount(); ++i) {
        const QMetaMethod m = meta->method(i);
        if (m.methodType() == QMetaMethod::Signal) {
            continue;
        }
        if (m.access() == QMetaMethod::Private) {
            continue;
        }
        if (m.name() == needle) {
            return m;
        }
    }
    return {};
}

// Find a signal by name. Same shape as findInvokableMethod, but
// filters to signals only. The subscribe path uses this for
// existence validation (typo'd signal names return NO_SUCH_SIGNAL
// at subscribe time instead of silently never delivering events).
// Walks the full inheritance chain so a signal declared on a base
// class is still resolvable.
QMetaMethod findSignal(const QMetaObject* meta, const QString& signalName)
{
    if (!meta) {
        return {};
    }
    const QByteArray needle = signalName.toUtf8();
    for (int i = 0; i < meta->methodCount(); ++i) {
        const QMetaMethod m = meta->method(i);
        if (m.methodType() != QMetaMethod::Signal) {
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
    // a quick connect — if nothing's listening, the path is dead
    // and we can safely remove + retry. If a live process IS
    // listening, the connect succeeds and we leave the path alone
    // (refuse to start). This avoids the "clobber a live listener
    // on the same path" bug a blind unlink would cause.
    m_server = std::make_unique<QLocalServer>(this);
    // We do NOT use setSocketOptions(UserAccessOption) here: that
    // option makes QLocalServer auto-unlink any existing socket
    // file before binding, which would silently clobber another
    // live listener on the same path. Instead we listen() against
    // the bare path and chmod the resulting socket file manually
    // below — same 0600 security outcome, but our stale-detection
    // path stays in control of unlink decisions.
    if (!m_server->listen(resolved)) {
        const QString initialError = m_server->errorString();
        bool removedStale = false;
        if (QFile::exists(resolved)) {
            // Probe via a fresh QLocalSocket: if a live router is
            // listening, the kernel-level connect handshake
            // completes near-instantly. If nothing's listening,
            // connect returns ECONNREFUSED and waitForConnected
            // reports false — only THEN do we unlink the stale
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
        if (!removedStale || !m_server->listen(resolved)) {
            qWarning("PhosphorIpc::IpcRouter::start: listen() failed for '%s': %s", qPrintable(resolved),
                     qPrintable(removedStale ? m_server->errorString() : initialError));
            m_server.reset();
            return false;
        }
    }
    // Restrict the socket to the owning user. $XDG_RUNTIME_DIR is
    // already 0700, but the socket file's own mode controls who
    // can connect; 0600 prevents an unprivileged co-tenant on the
    // same machine (e.g. a sidecar container sharing the runtime
    // dir) from connecting and invoking arbitrary registered
    // targets. Done after listen() rather than via
    // setSocketOptions(UserAccessOption) so our stale-detect path
    // remains the only thing that can unlink the file.
    QFile::setPermissions(resolved, QFile::ReadOwner | QFile::WriteOwner);
    m_socketPath = resolved;
    QObject::connect(m_server.get(), &QLocalServer::newConnection, this, &IpcRouter::handleNewConnection);
    return true;
}

void IpcRouter::stop()
{
    if (m_server) {
        m_server->close();
        m_server.reset();
    }
    m_subscriptionsBySocket.clear();
    m_socketPath.clear();
}

QString IpcRouter::socketPath() const
{
    return m_socketPath;
}

void IpcRouter::registerTarget(const QString& name, QObject* object)
{
    if (name.isEmpty()) {
        qWarning("PhosphorIpc::IpcRouter::registerTarget: empty name ignored");
        return;
    }
    if (!object) {
        qWarning("PhosphorIpc::IpcRouter::registerTarget: null object for '%s' ignored", qPrintable(name));
        return;
    }
    if (m_targets.contains(name)) {
        if (m_targets.value(name)) {
            qWarning("PhosphorIpc::IpcRouter::registerTarget: duplicate target '%s' ignored", qPrintable(name));
            return;
        }
        // Prior QObject was destroyed out from under us; the
        // QPointer has cleared. Treat the slot as vacant so the
        // caller can re-register a fresh QObject under the same
        // name. We still need to emit targetUnregistered so any
        // observer counts stay balanced.
        m_targets.remove(name);
        Q_EMIT targetUnregistered(name);
    }
    m_targets.insert(name, QPointer<QObject>(object));
    Q_EMIT targetRegistered(name);
}

void IpcRouter::unregisterTarget(const QString& name)
{
    if (m_targets.remove(name) > 0) {
        Q_EMIT targetUnregistered(name);
    }
}

QStringList IpcRouter::listTargets() const
{
    QStringList ids;
    ids.reserve(m_targets.size());
    for (auto it = m_targets.begin(); it != m_targets.end(); ++it) {
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
    QObject* obj = target(targetName);
    if (!obj) {
        return {};
    }
    return IpcSchemaGenerator::schemaFor(targetName, obj);
}

QVariant IpcRouter::invoke(const QString& targetName, const QString& fn, const QVariantList& args,
                           InvokeOutcome* outcome, QString* errorMessage)
{
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
    const QMetaMethod method = findInvokableMethod(obj->metaObject(), fn);
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
    QVariantList coerced;
    coerced.reserve(args.size());
    for (int i = 0; i < args.size(); ++i) {
        QVariant v = args.at(i);
        const QMetaType expected(method.parameterType(i));
        if (v.metaType() != expected) {
            if (!v.convert(expected)) {
                return fail(
                    InvokeOutcome::ArgConvertFailed,
                    QStringLiteral("arg %1 for '%2.%3': cannot convert %4 to %5")
                        .arg(i)
                        .arg(targetName, fn, QLatin1String(args.at(i).typeName()), QLatin1String(expected.name())));
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
    if (returnType.isValid() && returnType.id() != QMetaType::Void) {
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
    while (socket->canReadLine()) {
        QByteArray line = socket->readLine();
        // Strip the trailing newline; QJsonDocument tolerates it
        // but we keep parsing strict.
        while (!line.isEmpty() && (line.endsWith('\n') || line.endsWith('\r'))) {
            line.chop(1);
        }
        if (line.isEmpty()) {
            continue;
        }
        dispatch(socket, line);
    }
}

void IpcRouter::handleClientDisconnected(QLocalSocket* socket)
{
    if (!socket) {
        return;
    }
    // Drop the socket's subscription list — broadcastEvent skips
    // disconnected sockets anyway, but we explicitly free the
    // record so memory doesn't grow with churn.
    m_subscriptionsBySocket.remove(socket);
    socket->deleteLater();
}

void IpcRouter::dispatch(QLocalSocket* socket, const QByteArray& line)
{
    QString parseError;
    const auto reqOpt = parseRequest(line, &parseError);
    if (!reqOpt) {
        const QJsonObject err = buildError(0, QString::fromUtf8(ErrorCode::MalformedRequest), parseError);
        socket->write(writeLine(err));
        return;
    }
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

    // Subscription id == request id; clients use that on subsequent
    // unsubscribe and to demultiplex inbound events. The router
    // doesn't enforce uniqueness across all clients — id is scoped
    // per-socket because the wire identity is (socket, subscriptionId).
    Subscription sub;
    sub.socket = socket;
    sub.subscriptionId = id;
    sub.target = targetName;
    sub.signalName = signalName;
    m_subscriptionsBySocket[socket].append(sub);

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
    // Walk every subscriber set and forward to any subscription
    // matching (targetName, signalName). Iteration is O(N
    // subscriptions); for the expected per-process subscription
    // count (≤ ~50 in any realistic shell deployment) the linear
    // scan is faster than maintaining a secondary index.
    for (auto it = m_subscriptionsBySocket.begin(); it != m_subscriptionsBySocket.end(); ++it) {
        QLocalSocket* sock = it.key();
        if (!sock || sock->state() != QLocalSocket::ConnectedState) {
            continue;
        }
        for (const Subscription& sub : it.value()) {
            if (sub.target == targetName && sub.signalName == signalName) {
                sock->write(writeLine(buildEvent(sub.subscriptionId, args)));
                sock->flush();
            }
        }
    }
}

} // namespace PhosphorIpc
