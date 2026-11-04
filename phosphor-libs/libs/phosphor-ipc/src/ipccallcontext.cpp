// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorIpc/IpcCallContext.h>

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
    // QObject that it invokes, whatever the access of this base.
    return running && running->object == dynamic_cast<const void*>(this) ? running : nullptr;
}

bool IpcCallContext::calledFromIpc() const
{
    return call() != nullptr;
}

void IpcCallContext::sendErrorReply(const QString& message)
{
    if (Call* running = call()) {
        running->failed = true;
        running->message = message;
    }
}

} // namespace PhosphorIpc
