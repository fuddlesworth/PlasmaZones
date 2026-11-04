// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

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
    // the message. Outside a wire call it does nothing.
    void sendErrorReply(const QString& message);

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
    };
    static Call*& current();
    [[nodiscard]] Call* call() const;
};

} // namespace PhosphorIpc
