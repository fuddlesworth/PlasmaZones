// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include <PhosphorIpc/IpcCallContext.h>
#include <PhosphorIpc/phosphoripc_export.h>

#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QList>
#include <QObject>
#include <QPointer>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QVariant>
#include <QVariantList>
#include <QtCore/qtclasshelpermacros.h>

#include <memory>
#include <optional>

QT_BEGIN_NAMESPACE
class QLocalServer;
class QLocalSocket;
QT_END_NAMESPACE

namespace PhosphorIpc {

// Central JSON-over-Unix-socket dispatcher. Owns one QLocalServer and
// the registry of named callable QObject "targets". Speaks the
// NDJSON wire protocol defined in IpcProtocol.h. Single-threaded:
// must be used on the thread that owns it, normally the GUI thread
// (Qt's QLocalServer + the QMetaObject::invokeMethod path both assume
// that).
//
// Connections
//   - Each accepted client connection gets an id, unique in the process
//     and never reused (also across routers). A target reads the id of
//     the connection that runs its call with IpcCallContext::connectionId().
//     The id belongs to the router that runs the call: a target
//     registered on several routers sends its events through the one
//     that accepted the connection, or sendEvent() finds no connection.
//   - A call answers when its method returns, unless the method defers
//     its reply (IpcCallContext::deferReply, see IpcPendingReply.h).
//     Replies carry the request id, so a deferred reply may arrive after
//     the replies to later calls on the same connection.
//   - sendEvent() writes an event to one connection. Everything written
//     to a connection (replies, errors, events) arrives in write order.
//   - connectionClosed(id) follows the close of every connection.
//   - The router reads one request of a connection at a time. A method
//     that spins a nested event loop holds back every later request of
//     its own connection (calls, subscribes, list) until it returns, so
//     it must not wait there for another request of the same client:
//     defer the reply instead. Other connections are served meanwhile.
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
//   - Do not destroy the router from inside a call it runs or a signal
//     it emits (a target method, a connectionClosed slot): it is still
//     on the stack. stop() is fine there. So is deleteLater(), except
//     inside a nested event loop that a target method spins: that loop
//     runs the deletion, while the method's call is still on the stack.
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

    // The most deferred calls one connection may have waiting for their
    // answer (IpcCallContext::deferReply). Each holds router state and
    // whatever work its target keeps, so a client that pipelines calls to
    // a deferring method cannot grow them without bound. A call past the
    // cap fails with CALL_FAILED when its method returns. An answer, or
    // the close of the connection, makes room.
    static constexpr int MaxPendingCallsPerConnection = 256;

    // Bind the listening socket. Defaults to
    // $XDG_RUNTIME_DIR/phosphor.sock when socketPath is empty.
    // Returns false on bind failure; safe to retry after stop().
    bool start(const QString& socketPath = QString());

    // Close the listening socket and every client connection, each of
    // which then gets its connectionClosed. Idempotent. A target method
    // may call it: the connection whose call runs is closed too, and the
    // method's return value goes nowhere.
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
    // The subscriptions to the target end with it: each subscriber gets
    // {"type":"error","id":<subscriptionId>,"code":"NO_SUCH_TARGET",...}.
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
        // The method ran and failed through IpcCallContext::sendErrorReply.
        // The error message is the target's.
        CallFailed,
        // The method deferred its reply (IpcCallContext::deferReply). On
        // a direct invoke() there is no client to answer later: the return
        // value is dropped and the pending reply holds no call.
        Deferred,
    };

    // Synchronously invoke target.fn(args). Returns the function's
    // return value as a QVariant; on failure returns an invalid
    // QVariant and populates the optional outcome / message
    // out-params. Pass nullptr if the caller doesn't need them.
    // A target that inherits IpcCallContext runs the method as a wire
    // call, and can fail it with sendErrorReply (CallFailed). The call
    // comes from no connection: IpcCallContext::connectionId() is 0.
    QVariant invoke(const QString& target, const QString& fn, const QVariantList& args,
                    InvokeOutcome* outcome = nullptr, QString* errorMessage = nullptr);

    // Broadcast a JSON event to every connected subscriber that has
    // subscribed to (target, signalName). The IpcTarget QML type's
    // emitEvent() method is the canonical call site, plugin
    // authors call emitEvent("countChanged", [value]) whenever a
    // wire-visible state transition happens, which is more explicit
    // than auto-introspecting QObject signals. Called off the router's
    // thread it sends nothing (with a warning; debug builds assert).
    void broadcastEvent(const QString& target, const QString& signalName, const QJsonArray& args);

    // Write one event to the client connection `connectionId`, whether
    // or not it subscribed to anything:
    //   {"type":"event","target":<target>,"signal":<signalName>,"args":[...]}
    // The frame names the event in place of a subscriptionId, which it
    // has none of. It is meant for a call's own output, sent to the
    // client that made the call: take the id from
    // IpcCallContext::connectionId() while the call runs. Declare the
    // event as a signal on the target, as for broadcast events, so the
    // schema lists it. The schema then also offers the signal for
    // subscription, and a subscriber gets only what broadcastEvent sends
    // under that name: a target that wants both kinds of listener calls
    // both. The event goes out after everything written to the
    // connection before it, and before what is written after it, such as
    // the reply of IpcPendingReply::finish().
    //
    // Returns true when the event was queued on an open connection. A
    // client that went away before the router noticed loses it, and
    // connectionClosed follows. Returns false, and writes nothing, for an
    // empty target or signal name, for an id that names no open
    // connection of this router (unknown, or closed already), and when
    // called off the router's thread (with a warning; debug builds
    // assert). The same output limit applies as for broadcastEvent: a
    // connection more than 16 MiB behind is closed (with a warning) and
    // the call returns false.
    bool sendEvent(quint64 connectionId, const QString& target, const QString& signalName, const QJsonArray& args);

Q_SIGNALS:
    void targetRegistered(const QString& name);
    void targetUnregistered(const QString& name);
    // The client connection `connectionId` has closed: the client went
    // away, the router closed it (malformed input, output limit) or
    // stop() ran. Sent once per connection, from the event loop and
    // never synchronously from inside a call into the router (such as
    // sendEvent or stop), so a slot may end the connection's work and
    // destroy its pending replies right there. A target method that
    // spins a nested event loop can get it inside that loop, as it can
    // any queued signal. By then sendEvent() to the id returns false and
    // pending replies for it drop their answer.
    //
    // The router's destructor sends it, for the connections it closes,
    // before it returns. A receiver that holds the router as a member and
    // connects this to its own slot is then partly destroyed: members
    // declared after the router are gone. Declare the router after the
    // state the slot uses, or disconnect before the router goes.
    void connectionClosed(quint64 connectionId);

private:
    friend class IpcCallContext;
    friend class IpcPendingReply;

    // A deferred call waiting for its answer: where the answer goes.
    struct PendingCall
    {
        quint64 connectionId = 0;
        qint64 requestId = 0;
    };

    // invoke() for a call from `connectionId` (0: none) with request id
    // `requestId`, as the wire dispatcher makes it.
    QVariant invokeCall(const QString& target, const QString& fn, const QVariantList& args, quint64 connectionId,
                        qint64 requestId, InvokeOutcome* outcome, QString* errorMessage);
    // Registers the running call's pending reply. Called by
    // IpcCallContext::deferReply for a call from a connection. Past
    // MaxPendingCallsPerConnection it fails the call instead and returns a
    // pending reply that holds no call.
    IpcPendingReply deferCall(IpcCallContext::Call& call);
    // Answer a pending call once: the first answer takes the call out of
    // m_pendingCalls, and later ones find nothing. True when the answer
    // was queued on an open connection.
    bool finishPendingCall(quint64 token, const QJsonValue& result);
    bool failPendingCall(quint64 token, const QString& code, const QString& message);
    // Takes the pending call `token` out of the books, or nothing when no
    // call waits under it.
    std::optional<PendingCall> takePendingCall(quint64 token);
    // The open connection with this id, or nullptr.
    [[nodiscard]] QLocalSocket* connectedSocket(quint64 connectionId) const;
    // Book a closed connection for connectionClosed, which the event loop
    // (or the destructor) sends in announceClosedConnections.
    void connectionGone(quint64 connectionId);
    void announceClosedConnections();

    // Internal subscription record: (subscriptionId, target,
    // signalName), kept per socket in m_subscriptionsBySocket. Events
    // arrive via broadcastEvent() rather than auto-introspected Qt
    // signal connections (which clashed with Q_OBJECT moc-generated
    // qt_metacall, see the explicit-broadcast rationale in
    // IpcTarget::emitEvent).
    struct Subscription
    {
        qint64 subscriptionId = 0;
        QString target;
        QString signalName;
    };

    void handleNewConnection();
    // Runs readFrames for `socket`, unless a dispatch on it is on the
    // stack already (a target method spinning a nested event loop), and
    // deletes the socket afterwards when it closed meanwhile.
    void handleClientReadyRead(QLocalSocket* socket);
    void readFrames(QLocalSocket* socket);
    void handleClientDisconnected(QLocalSocket* socket);
    // Deletes a closed socket from the event loop, once no dispatch on
    // it is on the stack.
    void retireSocket(QLocalSocket* socket);
    // Ends every subscription to the target `name`: each subscriber gets
    // a NO_SUCH_TARGET error under its subscription id.
    void endSubscriptionsTo(const QString& name);
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
    // count exceeds detail::MaxConsecutiveMalformedFrames so a peer can't
    // pin the router on parse failures indefinitely.
    QHash<QLocalSocket*, int> m_malformedCountBySocket;
    // The id of every open connection, both ways. An entry goes when the
    // connection closes, so a lookup by id finds only open connections.
    QHash<QLocalSocket*, quint64> m_connectionIdBySocket;
    QHash<quint64, QPointer<QLocalSocket>> m_socketByConnectionId;
    // Deferred calls that wait for their answer, by pending-reply token,
    // and the tokens of each connection's calls. A connection's entries go
    // when it closes.
    QHash<quint64, PendingCall> m_pendingCalls;
    QHash<quint64, QSet<quint64>> m_pendingTokensByConnection;
    quint64 m_lastPendingToken = 0;
    // Sockets with a dispatch on the stack, and those of them that closed
    // meanwhile and are deleted when it unwinds. Deleting one earlier
    // frees it under the dispatch (and under Qt's readyRead emission).
    QSet<QLocalSocket*> m_socketsInDispatch;
    QSet<QLocalSocket*> m_socketsToDelete;
    // Closed connections that connectionClosed has not been sent for yet.
    QList<quint64> m_closedConnections;
};

} // namespace PhosphorIpc
