// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

// Subscribe / unsubscribe / broadcast / targeted-event paths for
// IpcRouter. All four methods are members of IpcRouter and reach into the
// same private state (m_subscriptionsBySocket); the split from ipcrouter.cpp is
// purely organisational. Meta-helpers and the per-socket cap constants
// live in ipcrouterdetail.h so dispatch() and handleSubscribe()
// agree on the introspection floor and the cap value.

#include <PhosphorIpc/IpcRouter.h>

#include "ipcrouterdetail.h"

#include <PhosphorIpc/IpcProtocol.h>

#include <QByteArray>
#include <QJsonArray>
#include <QJsonValue>
#include <QLocalSocket>
#include <QMetaMethod>
#include <QMetaObject>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QThread>

namespace PhosphorIpc {

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
    // typos surface as NO_SUCH_SIGNAL instead of silently accepting
    // a subscription that will never fire. Signal lookup here is
    // advisory: broadcastEvent dispatches by string name, not by
    // metaobject index, so a future signal added via QML dynamic
    // property would also work even without recompiling.
    if (!detail::findSignal(obj, signalName).isValid()) {
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
    if (subs.size() >= detail::MaxSubscriptionsPerSocket) {
        socket->write(writeLine(buildError(
            id, QString::fromUtf8(ErrorCode::MalformedRequest),
            QStringLiteral("subscription cap of %1 per socket exceeded").arg(detail::MaxSubscriptionsPerSocket))));
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

    // Write-side cap (detail::MaxBytesToWrite). When exceeded,
    // force-close the subscriber. handleClientDisconnected prunes its
    // subscription entries.
    //
    // The cap is checked BEFORE each write, so the effective ceiling
    // is "at least MaxBytesToWrite behind plus the size of one
    // event payload". A subscriber sitting at 15.9 MiB that receives
    // a 200 KiB event will end the iteration around 16.1 MiB, and
    // the next broadcast aimed at that subscriber will catch it.
    // This is acceptable for a UI shell — the alternative
    // (post-write recheck + abort) makes the worst-case latency
    // depend on the slowest single write, which is worse than a few
    // KB of slop on the ceiling.
    for (const PendingSend& p : pending) {
        QLocalSocket* sock = p.socket.data();
        // Socket may have disconnected mid-broadcast (a prior send's
        // flush in this loop fired a chained disconnected; the
        // QPointer auto-cleared). Skip without trying to write.
        if (!sock || sock->state() != QLocalSocket::ConnectedState) {
            continue;
        }
        if (sock->bytesToWrite() > detail::MaxBytesToWrite) {
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

bool IpcRouter::sendEvent(quint64 connectionId, const QString& targetName, const QString& signalName,
                          const QJsonArray& args)
{
    Q_ASSERT(thread() == QThread::currentThread());
    if (targetName.isEmpty() || signalName.isEmpty()) {
        return false;
    }
    QLocalSocket* sock = connectedSocket(connectionId);
    if (!sock) {
        return false;
    }
    // The broadcast cap and its close, for the same reason: a client that
    // stopped reading must not grow the write queue without bound. The
    // abort runs handleClientDisconnected at once, which books the
    // connection for connectionClosed; that signal comes from the event
    // loop, not from inside this call.
    if (sock->bytesToWrite() > detail::MaxBytesToWrite) {
        qWarning("PhosphorIpc::IpcRouter::sendEvent: connection %llu is %lld bytes behind; closing the connection",
                 static_cast<unsigned long long>(connectionId), static_cast<long long>(sock->bytesToWrite()));
        sock->abort();
        return false;
    }
    // No flush(), as in broadcastEvent: the write goes out from the event
    // loop, in order with everything else written to the socket.
    sock->write(writeLine(buildEvent(targetName, signalName, args)));
    return true;
}

} // namespace PhosphorIpc
