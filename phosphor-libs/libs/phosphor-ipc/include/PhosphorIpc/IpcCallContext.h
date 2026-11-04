// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include <PhosphorIpc/IpcPendingReply.h>
#include <PhosphorIpc/phosphoripc_export.h>

#include <QString>

namespace PhosphorIpc {

class IpcRouter;

// Lets a target fail the wire call that runs it, the way QDBusContext
// lets a D-Bus adaptor send an error reply. A target class inherits it
// next to QObject:
//
//     class Control : public QObject, protected PhosphorIpc::IpcCallContext
//     {
//         Q_OBJECT
//     public:
//         Q_INVOKABLE int focus(int pane)
//         {
//             if (!exists(pane)) {
//                 sendErrorReply(QStringLiteral("no pane %1").arg(pane));
//                 return 0;
//             }
//             ...
//         }
//     };
//
// The client then gets {"type":"error","code":"CALL_FAILED","message":...}
// in place of the reply, and the method's return value is dropped.
// IpcRouter::invoke reports InvokeOutcome::CallFailed with the message.
//
// The base can be protected, as above. The router records the running call
// for the target's most-derived object (dynamic_cast<const void*>, so the
// target must be built with RTTI, Qt's default), and a context answers
// only for its own object. The record lives on the router's thread for
// the length of the call. A method that runs for any other reason (a
// direct C++ call, a queued slot, a call into another object from inside
// a wire call) sees no call: calledFromIpc() is false and
// sendErrorReply() does nothing.
//
// A call that cannot answer before its method returns (it runs a process,
// waits for the user) defers its reply: deferReply() returns an
// IpcPendingReply that the target keeps and finishes later. See
// IpcPendingReply.h for the rules. connectionId() names the client
// connection that made the call, so a target can send that client events of
// its own (IpcRouter::sendEvent) and drop the client's work when the
// connection closes (IpcRouter::connectionClosed).
class PHOSPHORIPC_EXPORT IpcCallContext
{
public:
    virtual ~IpcCallContext();

protected:
    IpcCallContext() = default;
    IpcCallContext(const IpcCallContext&) = delete;
    IpcCallContext& operator=(const IpcCallContext&) = delete;

    // True while the router runs a wire call on this object.
    [[nodiscard]] bool calledFromIpc() const;

    // Fails the running wire call with `message`. A second call replaces
    // the message. Outside a wire call it does nothing. After deferReply()
    // the error still goes out when the method returns, unless the pending
    // reply has answered the call already, and the pending reply then has
    // nothing left to answer.
    void sendErrorReply(const QString& message);

    // The connection that made the running wire call: a number the router
    // gives each client connection it accepts, unique in the process and
    // never reused, also across routers. 0 outside a wire call and for a
    // call that came from no connection (a direct IpcRouter::invoke).
    [[nodiscard]] quint64 connectionId() const;

    // Defers the running wire call's reply. The router sends nothing when
    // the method returns, and drops its return value. The call is answered
    // through the returned object (finish or fail) or, if that object goes
    // away unanswered, with a CALL_ABANDONED error.
    //
    // Returns an object that holds no call (isValid() false) outside a wire
    // call, on a second deferReply() in the same call (with a warning), and
    // for a call from no connection: IpcRouter::invoke then reports
    // InvokeOutcome::Deferred, since there is no client to answer later.
    [[nodiscard]] IpcPendingReply deferReply();

private:
    friend class IpcRouter;

    // One wire call that runs on this thread. A call made from inside
    // another (a nested event loop) keeps the outer one to restore.
    struct Call
    {
        const void* object = nullptr;
        bool failed = false;
        QString message;
        Call* outer = nullptr;
        // The router that runs the call, and the connection and request it
        // came from (0 and 0 for a direct IpcRouter::invoke).
        IpcRouter* router = nullptr;
        quint64 connectionId = 0;
        qint64 requestId = 0;
        // Set by deferReply(). pendingToken is the router's key for the
        // pending reply, 0 when the call came from no connection.
        bool deferred = false;
        quint64 pendingToken = 0;
    };
    static Call*& current();
    [[nodiscard]] Call* call() const;
};

} // namespace PhosphorIpc
