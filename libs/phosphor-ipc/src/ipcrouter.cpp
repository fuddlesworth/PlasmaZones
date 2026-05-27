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

namespace PhosphorIpc {

namespace {

// Convert a QVariant — typically a sync invoke() return value or a
// signal argument — into a JSON value. Recursive on QVariantList /
// QVariantMap. Falls back to toString() for unknown types so the
// wire never contains an opaque "QObject*" placeholder. Returns a
// QJsonValue::Null when the input is invalid (no return value /
// void-returning function).
QJsonValue variantToJson(const QVariant& v)
{
    if (!v.isValid()) {
        return QJsonValue::Null;
    }
    switch (v.typeId()) {
    case QMetaType::Bool:
        return v.toBool();
    case QMetaType::Int:
    case QMetaType::UInt:
    case QMetaType::Long:
    case QMetaType::ULong:
    case QMetaType::Short:
    case QMetaType::UShort:
    case QMetaType::LongLong:
        return static_cast<double>(v.toLongLong());
    case QMetaType::ULongLong:
        return static_cast<double>(v.toULongLong());
    case QMetaType::Double:
    case QMetaType::Float:
        return v.toDouble();
    case QMetaType::QString:
        return v.toString();
    case QMetaType::QStringList: {
        QJsonArray arr;
        for (const QString& s : v.toStringList()) {
            arr.append(s);
        }
        return arr;
    }
    case QMetaType::QVariantList: {
        QJsonArray arr;
        for (const QVariant& item : v.toList()) {
            arr.append(variantToJson(item));
        }
        return arr;
    }
    case QMetaType::QVariantMap: {
        QJsonObject obj;
        const QVariantMap map = v.toMap();
        for (auto it = map.begin(); it != map.end(); ++it) {
            obj.insert(it.key(), variantToJson(it.value()));
        }
        return obj;
    }
    case QMetaType::QJsonValue:
        return v.toJsonValue();
    case QMetaType::QJsonObject:
        return v.toJsonObject();
    case QMetaType::QJsonArray:
        return v.toJsonArray();
    default:
        // Unknown type — emit the string-shaped fallback so the
        // wire doesn't gain undocumented JSON shapes per metatype.
        return v.toString();
    }
}

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
    if (!m_server->listen(resolved)) {
        bool removedStale = false;
        if (QFile::exists(resolved)) {
            QLocalSocket probe;
            probe.connectToServer(resolved);
            const bool aliveListener = probe.waitForConnected(100);
            probe.abort();
            if (!aliveListener) {
                QFile::remove(resolved);
                removedStale = true;
            }
        }
        if (!removedStale || !m_server->listen(resolved)) {
            qWarning("PhosphorIpc::IpcRouter::start: listen() failed for '%s': %s", qPrintable(resolved),
                     qPrintable(m_server->errorString()));
            m_server.reset();
            return false;
        }
    }
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
    if (m_targets.contains(name) && m_targets.value(name)) {
        qWarning("PhosphorIpc::IpcRouter::registerTarget: duplicate target '%s' ignored", qPrintable(name));
        return;
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

QVariant IpcRouter::invoke(const QString& targetName, const QString& fn, const QVariantList& args, QString* errorOut)
{
    QObject* obj = target(targetName);
    if (!obj) {
        if (errorOut) {
            *errorOut = QStringLiteral("unknown target '%1'").arg(targetName);
        }
        return {};
    }
    const QMetaMethod method = findInvokableMethod(obj->metaObject(), fn);
    if (!method.isValid()) {
        if (errorOut) {
            *errorOut = QStringLiteral("target '%1' has no invokable method '%2'").arg(targetName, fn);
        }
        return {};
    }
    if (method.parameterCount() != args.size()) {
        if (errorOut) {
            *errorOut = QStringLiteral("argument count mismatch: '%1.%2' expects %3, got %4")
                            .arg(targetName, fn)
                            .arg(method.parameterCount())
                            .arg(args.size());
        }
        return {};
    }

    // Build the arg array. Up to 10 args (Qt's invokeMethod limit
    // for QGenericArgument-based dispatch; QML-declared functions
    // rarely exceed this in practice). For each arg, coerce the
    // incoming QVariant to the method's expected parameter type.
    constexpr int kMaxArgs = 10;
    if (args.size() > kMaxArgs) {
        if (errorOut) {
            *errorOut = QStringLiteral("argument count exceeds dispatch limit of %1").arg(kMaxArgs);
        }
        return {};
    }
    QVariantList coerced;
    coerced.reserve(args.size());
    for (int i = 0; i < args.size(); ++i) {
        QVariant v = args.at(i);
        const QMetaType expected(method.parameterType(i));
        if (v.metaType() != expected) {
            if (!v.convert(expected)) {
                if (errorOut) {
                    *errorOut =
                        QStringLiteral("arg %1 for '%2.%3': cannot convert %4 to %5")
                            .arg(i)
                            .arg(targetName, fn, QLatin1String(args.at(i).typeName()), QLatin1String(expected.name()));
                }
                return {};
            }
        }
        coerced.append(std::move(v));
    }

    QGenericArgument g[kMaxArgs];
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
        if (errorOut) {
            *errorOut =
                QStringLiteral("invocation of '%1.%2' failed (QMetaMethod::invoke returned false)").arg(targetName, fn);
        }
        return {};
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
        QString invokeError;
        const QVariant result = invoke(req.target, req.fn, req.args, &invokeError);
        if (!invokeError.isEmpty()) {
            // Distinguish "no target" so the CLI can produce a
            // sharper exit code. Other invocation failures
            // (arg-type mismatch, invoke false) collapse under
            // INVALID_ARG / INVOCATION_FAILED.
            QString code = QString::fromUtf8(ErrorCode::InvocationFailed);
            if (!target(req.target)) {
                code = QString::fromUtf8(ErrorCode::NoSuchTarget);
            } else if (invokeError.contains(QStringLiteral("no invokable method"))) {
                code = QString::fromUtf8(ErrorCode::NoSuchFn);
            } else if (invokeError.contains(QStringLiteral("cannot convert"))
                       || invokeError.contains(QStringLiteral("argument count"))) {
                code = QString::fromUtf8(ErrorCode::InvalidArg);
            }
            socket->write(writeLine(buildError(req.id, code, invokeError)));
            return;
        }
        socket->write(writeLine(buildReply(req.id, variantToJson(result))));
        return;
    }
    if (req.type == QLatin1String(RequestType::Subscribe) || req.type == QLatin1String(RequestType::Unsubscribe)) {
        // Subscribe semantics wire up in a follow-on commit on
        // this branch. For now reply with a clear error so the
        // CLI can surface "subscribe not yet implemented" without
        // hanging the connection.
        socket->write(writeLine(buildError(req.id, QString::fromUtf8(ErrorCode::MalformedRequest),
                                           QStringLiteral("subscribe/unsubscribe not yet implemented"))));
        return;
    }
    socket->write(writeLine(buildError(req.id, QString::fromUtf8(ErrorCode::MalformedRequest),
                                       QStringLiteral("unknown request type '%1'").arg(req.type))));
}

} // namespace PhosphorIpc
