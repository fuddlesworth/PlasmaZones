// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include <PhosphorIpc/phosphoripc_export.h>

#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QStringList>
#include <QVariant>
#include <QVariantList>
#include <QtCore/qtclasshelpermacros.h>

#include <memory>

QT_BEGIN_NAMESPACE
class QLocalServer;
class QLocalSocket;
QT_END_NAMESPACE

namespace PhosphorIpc {

// Central JSON-over-Unix-socket dispatcher. Owns one QLocalServer and
// the registry of named callable QObject "targets". Speaks the
// NDJSON wire protocol defined in IpcProtocol.h. Single-threaded:
// must be constructed and used on the GUI thread (Qt's QLocalServer
// + the QMetaObject::invokeMethod path both assume that).
//
// Lifetime
//   - Application owns one IpcRouter (typically constructed in main()
//     alongside the QQmlApplicationEngine).
//   - Targets register their backing QObjects via registerTarget;
//     unregisterTarget drops them. Target QObjects must outlive their
//     registration (QPointer tracking handles dangling-pointer paths
//     defensively, but the contract is "drop the registration before
//     destroying the QObject").
//   - Subscribers are tracked per-(target, signal-name, socket)
//     tuple. Socket disconnect auto-prunes the subscriber set.
//
// Errors
//   - start() returns false on bind failure (path conflict, perms,
//     missing XDG dir). The application logs the reason and
//     continues without IPC; failure is non-fatal.
//   - All wire-level errors are reported back to the client as
//     {"type":"error",...} responses (see IpcProtocol::ErrorCode).
class PHOSPHORIPC_EXPORT IpcRouter : public QObject
{
    Q_OBJECT
public:
    explicit IpcRouter(QObject* parent = nullptr);
    ~IpcRouter() override;
    Q_DISABLE_COPY_MOVE(IpcRouter)

    // Bind the listening socket. Defaults to
    // $XDG_RUNTIME_DIR/phosphor.sock when socketPath is empty.
    // Returns false on bind failure; safe to retry after stop().
    bool start(const QString& socketPath = QString());

    // Close the listening socket. Existing connections drain on
    // their own (subscribers stop receiving events when the
    // socket disconnects). Idempotent.
    void stop();

    // The resolved socket path (after XDG fallback). Empty if
    // start() has not been called or failed.
    [[nodiscard]] QString socketPath() const;

    // Register a target QObject under a name. Returns true if the
    // registry now binds `name` to `object` (a fresh registration,
    // OR an idempotent re-register of the same object under the same
    // name — silent success, no warning). Returns false when the
    // call is rejected (empty name, null object, or a different
    // object already owns the name; the latter emits a qWarning).
    // Marked nodiscard because callers that must pair register with
    // unregister (e.g. IpcTarget's componentComplete/destructor
    // pairing) NEED the result to know whether the registration is
    // theirs to undo.
    [[nodiscard]] bool registerTarget(const QString& name, QObject* object);
    // Unregister the entry under `name` only if it currently binds
    // to `object`. Mismatched calls are silently ignored so a target
    // whose registration was rejected (duplicate name) cannot
    // accidentally tear down the legitimate owner's binding on
    // destruction. Pass nullptr to drop the entry unconditionally
    // (administrative use, e.g. the shell's shutdown path).
    void unregisterTarget(const QString& name, QObject* object = nullptr);

    [[nodiscard]] QStringList listTargets() const;

    // Look up the live QObject backing a target. Returns nullptr if
    // the target is unknown or its backing QObject was destroyed
    // out from under us (QPointer auto-clears).
    [[nodiscard]] QObject* target(const QString& name) const;

    // Generate the JSON Schema for one target. Walks the target
    // QObject's metaobject; see IpcSchemaGenerator.h for the
    // QMetaType → JSON Schema mapping. For unknown targets, returns
    // the empty-shaped document `{target: "<name>", functions: [],
    // signals: []}` (matches `IpcSchemaGenerator::schemaFor(name,
    // nullptr)`). The wire dispatcher still replies with a
    // NO_SUCH_TARGET error frame for `schema` requests on unknown
    // targets; this convenience accessor returns the same shape a
    // schema-generator caller would see directly.
    [[nodiscard]] QJsonObject schemaFor(const QString& target) const;

    // Structured outcome of an invoke() call. The wire dispatcher
    // maps these onto IpcProtocol::ErrorCode strings without
    // string-matching the human-readable message.
    enum class InvokeOutcome {
        Ok,
        NoSuchTarget,
        NoSuchFn,
        ArgCountMismatch,
        ArgConvertFailed,
        InvokeFailed,
    };

    // Synchronously invoke target.fn(args). Returns the function's
    // return value as a QVariant; on failure returns an invalid
    // QVariant and populates the optional outcome / message
    // out-params. Pass nullptr if the caller doesn't need them.
    QVariant invoke(const QString& target, const QString& fn, const QVariantList& args,
                    InvokeOutcome* outcome = nullptr, QString* errorMessage = nullptr);

    // Broadcast a JSON event to every connected subscriber that has
    // subscribed to (target, signalName). The IpcTarget QML type's
    // emitEvent() method is the canonical call site, plugin
    // authors call emitEvent("countChanged", [value]) whenever a
    // wire-visible state transition happens, which is more explicit
    // than auto-introspecting QObject signals.
    void broadcastEvent(const QString& target, const QString& signalName, const QJsonArray& args);

Q_SIGNALS:
    void targetRegistered(const QString& name);
    void targetUnregistered(const QString& name);

private:
    // Internal subscription record. Subscribers store (socket,
    // subscriptionId, target, signalName) tuples; events arrive
    // via broadcastEvent() rather than auto-introspected Qt
    // signal connections (which clashed with Q_OBJECT moc-generated
    // qt_metacall, see the explicit-broadcast rationale in
    // IpcTarget::emitEvent).
    struct Subscription
    {
        QPointer<QLocalSocket> socket;
        qint64 subscriptionId = 0;
        QString target;
        QString signalName;
    };

    void handleNewConnection();
    void handleClientReadyRead(QLocalSocket* socket);
    void handleClientDisconnected(QLocalSocket* socket);
    void dispatch(QLocalSocket* socket, const QByteArray& line);
    void handleSubscribe(QLocalSocket* socket, qint64 id, const QString& targetName, const QString& signalName);
    void handleUnsubscribe(QLocalSocket* socket, qint64 id, qint64 subscriptionId);
    // Emit a MALFORMED_REQUEST diagnostic to `socket`, give the
    // bytes a brief window to land in the kernel send buffer, then
    // abort() the connection. Shared by the read-side oversize-line
    // and malformed-frame-cap paths so they stay in lockstep if the
    // close protocol ever changes (e.g. switching from abort() to
    // disconnectFromServer()).
    void closeWithMalformedDiagnostic(QLocalSocket* socket, const QString& message);

    std::unique_ptr<QLocalServer> m_server;
    QString m_socketPath;
    QHash<QString, QPointer<QObject>> m_targets;
    // Per-socket subscription list; lets disconnect cleanup walk one
    // socket's subscriptions without scanning the whole router.
    QHash<QLocalSocket*, QList<Subscription>> m_subscriptionsBySocket;
    // Per-socket consecutive malformed-frame counter. Reset to zero
    // on a successfully-parsed frame; closes the connection when the
    // count exceeds MaxConsecutiveMalformedFrames so a peer can't
    // pin the router on parse failures indefinitely.
    QHash<QLocalSocket*, int> m_malformedCountBySocket;
};

} // namespace PhosphorIpc
