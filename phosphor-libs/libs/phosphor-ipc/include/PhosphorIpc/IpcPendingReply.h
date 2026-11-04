// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include <PhosphorIpc/phosphoripc_export.h>

#include <QJsonValue>
#include <QPointer>
#include <QString>

namespace PhosphorIpc {

class IpcRouter;

// The answer to a wire call that a target gives later, after its method
// returned. IpcCallContext::deferReply() makes one inside the method, before
// the work starts, so that work that ends at once can answer through it:
//
//     Q_INVOKABLE void install(const QString& package)
//     {
//         // std::list: a job stays where it is while others come and go.
//         Job& job = m_jobs.emplace_back();
//         job.reply = deferReply();
//         startInstall(job, package); // answers through job.reply when it ends
//     }
//
// Keep one pending reply per call. A single member that every call
// assigns to abandons the call it held when the next call comes.
//
// The router sends no reply when such a method returns, and the method's
// return value is dropped. The client gets at most one answer for the call:
// the first of finish(), fail(), the CALL_ABANDONED error this object sends
// when it is destroyed or overwritten unanswered, and the error of an
// IpcCallContext::sendErrorReply the method made before it returned. A
// connection that closes first gets none. Replies to the connection's other
// calls go out meanwhile, as their methods return, so a deferred reply may
// arrive after replies to calls made later. Every reply carries the
// request's id, and a client that has more than one call in flight on a
// connection must give each its own id, one that no live subscription on
// the connection uses either (the error that ends a subscription carries
// the subscription id). A call sent without an id has id 0: finish() then
// answers with "id":0, and fail() with an error that carries no id, like
// any error for a request without one.
//
// Everything the router writes to one connection goes out in the order it
// was written: an event sent with IpcRouter::sendEvent before finish() reaches
// the client before the reply.
//
// A pending reply for a connection that has closed is still valid: finish()
// and fail() drop the answer and return false, without a warning. The same
// holds after the router has been destroyed.
//
// Move-only. Use it on the router's thread only, including its destruction:
// it writes to the connection directly. Debug builds assert this. Release
// builds warn, and finish() and fail() return false and keep the call, which
// the object can still answer on the router's thread. A call that is never
// answered there (the object is destroyed off the thread too) waits until
// its connection closes, and holds one of the connection's
// IpcRouter::MaxPendingCallsPerConnection places meanwhile.
class PHOSPHORIPC_EXPORT IpcPendingReply
{
public:
    // Holds no call. finish() and fail() do nothing and return false.
    IpcPendingReply();
    // An unanswered call fails with CALL_ABANDONED, so the client does not
    // wait for an answer that never comes.
    ~IpcPendingReply();
    IpcPendingReply(IpcPendingReply&& other) noexcept;
    // Abandons the call this object held, as the destructor does, then takes
    // over the call of `other`.
    IpcPendingReply& operator=(IpcPendingReply&& other) noexcept;
    IpcPendingReply(const IpcPendingReply&) = delete;
    IpcPendingReply& operator=(const IpcPendingReply&) = delete;

    // True while the object holds a call: from deferReply() until finish(),
    // fail() or a move away. False for a default-constructed object and for
    // every deferral that holds no call (IpcCallContext::deferReply lists
    // them: outside a call, a second deferral, a call from no connection,
    // past IpcRouter::MaxPendingCallsPerConnection). It does not say that the call can still be
    // answered: the connection may have closed, or the method may have
    // failed the call with sendErrorReply, and finish() then returns false.
    [[nodiscard]] bool isValid() const;

    // The connection that made the call, as IpcCallContext::connectionId()
    // reported it, also after the call was answered: the id to send the
    // call's events to (IpcRouter::sendEvent). 0 for an object that never
    // held a call and for a moved-from one.
    [[nodiscard]] quint64 connectionId() const;

    // Sends {"type":"reply","id":<request id>,"result":<result>}. An
    // undefined result goes out as null. Returns true when the reply was
    // queued on an open connection, false when there was nothing to answer
    // (already answered, no call), when the connection has closed, and off
    // the router's thread. A client that went away before the router
    // noticed loses a reply that returned true.
    bool finish(const QJsonValue& result = QJsonValue());

    // Sends {"type":"error","id":<request id>,"code":"CALL_FAILED",
    // "message":<message>}, the error IpcCallContext::sendErrorReply sends.
    // Returns as finish() does.
    bool fail(const QString& message);

    // Sends an error with the target's own `code`, for a failure a client is
    // meant to tell apart from others. An empty code sends CALL_FAILED. The
    // code goes out as given: pick one outside IpcProtocol's ErrorCode list,
    // whose codes name what the router found, so that a client can tell the
    // router's errors from the target's. Returns as finish() does.
    bool fail(const QString& code, const QString& message);

private:
    friend class IpcRouter;
    IpcPendingReply(IpcRouter* router, quint64 token, quint64 connectionId);

    // Answers the call with CALL_ABANDONED when this object still holds it.
    void abandon();
    // Gives up the call for answering: its token, or 0 when there is
    // nothing to answer here (no call, router gone, wrong thread).
    [[nodiscard]] quint64 takeToken();

    QPointer<IpcRouter> m_router;
    // The router's key for the call. 0 when this object holds none.
    quint64 m_token = 0;
    quint64 m_connectionId = 0;
};

} // namespace PhosphorIpc
