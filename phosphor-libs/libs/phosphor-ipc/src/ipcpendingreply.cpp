// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorIpc/IpcPendingReply.h>

#include <PhosphorIpc/IpcProtocol.h>
#include <PhosphorIpc/IpcRouter.h>

#include <QThread>

#include <utility>

namespace PhosphorIpc {

IpcPendingReply::IpcPendingReply() = default;

IpcPendingReply::IpcPendingReply(IpcRouter* router, quint64 token, quint64 connectionId)
    : m_router(router)
    , m_token(token)
    , m_connectionId(connectionId)
{
}

IpcPendingReply::~IpcPendingReply()
{
    abandon();
}

IpcPendingReply::IpcPendingReply(IpcPendingReply&& other) noexcept
    : m_router(std::move(other.m_router))
    , m_token(std::exchange(other.m_token, 0))
    , m_connectionId(std::exchange(other.m_connectionId, 0))
{
}

IpcPendingReply& IpcPendingReply::operator=(IpcPendingReply&& other) noexcept
{
    if (this != &other) {
        abandon();
        m_router = std::move(other.m_router);
        m_token = std::exchange(other.m_token, 0);
        m_connectionId = std::exchange(other.m_connectionId, 0);
    }
    return *this;
}

bool IpcPendingReply::isValid() const
{
    return m_token != 0;
}

quint64 IpcPendingReply::connectionId() const
{
    return m_connectionId;
}

quint64 IpcPendingReply::takeToken()
{
    if (!m_router) {
        // The router went, and with it the call: nothing can answer it.
        m_token = 0;
        return 0;
    }
    if (m_token == 0) {
        return 0;
    }
    // The router's books are not thread-safe. Off its thread the call
    // stays here, for an answer on the router's thread.
    const bool onRouterThread = m_router->thread() == QThread::currentThread();
    Q_ASSERT(onRouterThread);
    if (!onRouterThread) {
        qWarning("PhosphorIpc::IpcPendingReply: used off the router's thread; the call is not answered here");
        return 0;
    }
    // Give up the call before answering: whatever comes next, a second
    // finish() or fail() finds nothing to answer.
    return std::exchange(m_token, 0);
}

bool IpcPendingReply::finish(const QJsonValue& result)
{
    const quint64 token = takeToken();
    return token != 0 && m_router->finishPendingCall(token, result);
}

bool IpcPendingReply::fail(const QString& message)
{
    return fail(QString(), message);
}

bool IpcPendingReply::fail(const QString& code, const QString& message)
{
    const quint64 token = takeToken();
    return token != 0
        && m_router->failPendingCall(token, code.isEmpty() ? QString::fromUtf8(ErrorCode::CallFailed) : code, message);
}

void IpcPendingReply::abandon()
{
    fail(QString::fromUtf8(ErrorCode::CallAbandoned),
         QStringLiteral("the target dropped the call without answering it"));
}

} // namespace PhosphorIpc
