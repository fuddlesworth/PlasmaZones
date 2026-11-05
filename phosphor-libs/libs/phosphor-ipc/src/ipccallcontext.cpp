// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorIpc/IpcCallContext.h>

#include <PhosphorIpc/IpcRouter.h>

#include <QThread>
#include <QtGlobal>

namespace PhosphorIpc {

IpcCallContext::~IpcCallContext() = default;

IpcCallContext::Call*& IpcCallContext::current()
{
    thread_local Call* call = nullptr;
    return call;
}

IpcCallContext::Call* IpcCallContext::call() const
{
    Call* running = current();
    // The most-derived object: the router records the same address for the
    // QObject that it invokes, whatever the access of this base. The loop
    // level tells the method itself from a slot that a nested event loop
    // inside it runs on the same object.
    return running && running->object == dynamic_cast<const void*>(this)
            && running->loopLevel == QThread::currentThread()->loopLevel()
        ? running
        : nullptr;
}

bool IpcCallContext::calledFromIpc() const
{
    return call() != nullptr;
}

void IpcCallContext::warnIfNestedInCall(const char* what) const
{
    const int level = QThread::currentThread()->loopLevel();
    for (const Call* running = current(); running; running = running->outer) {
        if (running->object == dynamic_cast<const void*>(this) && running->loopLevel != level) {
            qWarning(
                "PhosphorIpc::IpcCallContext::%s: called from a nested event loop inside a wire call on this "
                "object, not from the call; it does nothing (defer the reply instead of waiting in a loop)",
                what);
            return;
        }
    }
}

void IpcCallContext::sendErrorReply(const QString& message)
{
    if (Call* running = call()) {
        running->failed = true;
        running->message = message;
        return;
    }
    warnIfNestedInCall("sendErrorReply");
}

quint64 IpcCallContext::connectionId() const
{
    const Call* running = call();
    return running ? running->connectionId : 0;
}

IpcPendingReply IpcCallContext::deferReply()
{
    Call* running = call();
    if (!running) {
        warnIfNestedInCall("deferReply");
        return {};
    }
    if (running->deferred) {
        // The first pending reply holds the call. Handing out a second
        // would give the client two answers to one request.
        qWarning(
            "PhosphorIpc::IpcCallContext::deferReply: the call is deferred already; the second deferral holds "
            "no call");
        return {};
    }
    running->deferred = true;
    // A direct IpcRouter::invoke has no client to answer later.
    if (running->connectionId == 0 || !running->router) {
        return {};
    }
    return running->router->deferCall(*running);
}

} // namespace PhosphorIpc
