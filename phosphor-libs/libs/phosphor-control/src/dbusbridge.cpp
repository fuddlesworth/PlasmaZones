// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include "PhosphorControl/DBusBridge.h"

#include <QDebug>
#include <QThread>
#include <QtDBus/QDBusConnection>
#include <QtDBus/QDBusPendingCall>
#include <QtDBus/QDBusPendingCallWatcher>
#include <QtDBus/QDBusPendingReply>

namespace PhosphorControl {

namespace {

// CLAUDE.md "Input validation at system boundaries" — D-Bus is a
// system boundary. An empty service/path/interface produces a
// malformed QDBusMessage that fails silently in async mode and
// returns an empty QDBusMessage in sync mode; reject explicitly so
// the programmer error surfaces in the log instead of buried in a
// no-op return.
bool validateEndpoint(const DBusEndpoint& endpoint, const QString& interfaceName, const QString& method,
                      const char* callSite)
{
    if (endpoint.service.isEmpty()) {
        qWarning() << callSite << ": refusing call — empty service";
        return false;
    }
    if (endpoint.objectPath.isEmpty()) {
        qWarning() << callSite << ": refusing call — empty objectPath";
        return false;
    }
    if (interfaceName.isEmpty()) {
        qWarning() << callSite << ": refusing call — empty interface";
        return false;
    }
    if (method.isEmpty()) {
        qWarning() << callSite << ": refusing call — empty method";
        return false;
    }
    return true;
}

} // namespace

DBusBridge::DBusBridge(DBusEndpoint endpoint, QObject* parent)
    : QObject(parent)
    , m_endpoint(std::move(endpoint))
{
    // QDBus interprets non-positive sync timeouts inconsistently
    // across Qt 6 patch releases (some treat <=0 as block-forever,
    // others as instant-fail). Clamp to a positive default so
    // syncTimeoutMs is always a usable value.
    if (m_endpoint.syncTimeoutMs <= 0) {
        qWarning() << "PhosphorControl::DBusBridge: non-positive syncTimeoutMs" << m_endpoint.syncTimeoutMs
                   << "— clamping to default" << DefaultSyncTimeoutMs << "ms";
        m_endpoint.syncTimeoutMs = DefaultSyncTimeoutMs;
    }
    // Surface programmer errors (forgot to fill in the endpoint) at the
    // construction site rather than via N per-call warnings later. Empty
    // interfaceName is tolerated here because consumers that always pass
    // an explicit interface to callOn() / asyncCallOn() have no use for a
    // default.
    if (m_endpoint.service.isEmpty() || m_endpoint.objectPath.isEmpty()) {
        qWarning() << "PhosphorControl::DBusBridge: endpoint has empty service or objectPath — "
                      "every call() will be rejected. service="
                   << m_endpoint.service << "objectPath=" << m_endpoint.objectPath
                   << "interfaceName=" << m_endpoint.interfaceName;
    }
}

DBusBridge::~DBusBridge() = default;

DBusEndpoint DBusBridge::endpoint() const
{
    return m_endpoint;
}

QDBusMessage DBusBridge::call(const QString& method, const QVariantList& args) const
{
    return callOn(m_endpoint.interfaceName, method, args);
}

QDBusMessage DBusBridge::callOn(const QString& interfaceName, const QString& method, const QVariantList& args) const
{
    if (!validateEndpoint(m_endpoint, interfaceName, method, "PhosphorControl::DBusBridge::callOn")) {
        return QDBusMessage::createError(QDBusError::InvalidArgs, QStringLiteral("DBusBridge: invalid call inputs"));
    }
    // QDBusConnection::sessionBus() returns a per-thread connection. A sync
    // call from a thread that didn't register one returns a Disconnected
    // error message silently — reject loudly instead, symmetric with
    // asyncCallOn below.
    if (thread() != QThread::currentThread()) {
        qWarning() << "PhosphorControl::DBusBridge::callOn: refused —"
                      "called from a thread other than the bridge's owning thread";
        return QDBusMessage::createError(QDBusError::Failed,
                                         QStringLiteral("DBusBridge: sync call from non-owning thread"));
    }
    QDBusMessage msg = QDBusMessage::createMethodCall(m_endpoint.service, m_endpoint.objectPath, interfaceName, method);
    if (!args.isEmpty()) {
        msg.setArguments(args);
    }
    return QDBusConnection::sessionBus().call(msg, QDBus::Block, m_endpoint.syncTimeoutMs);
}

void DBusBridge::asyncCall(const QString& method, const QVariantList& args)
{
    asyncCallOn(m_endpoint.interfaceName, method, args);
}

void DBusBridge::asyncCallOn(const QString& interfaceName, const QString& method, const QVariantList& args)
{
    if (!validateEndpoint(m_endpoint, interfaceName, method, "PhosphorControl::DBusBridge::asyncCallOn")) {
        return;
    }
    // QDBusConnection::sessionBus() returns a per-thread connection; the
    // watcher's deleteLater() relies on the event loop on the thread that
    // owns it. Calling asyncCallOn from a worker thread would attach the
    // watcher to that thread's loop — usually unintended for a settings-app
    // bridge that expects single-threaded UI usage. Reject explicitly so a
    // release build doesn't silently leak watchers or trigger Qt's
    // cross-thread-parent warning.
    if (thread() != QThread::currentThread()) {
        qWarning() << "PhosphorControl::DBusBridge::asyncCallOn: refused —"
                      "called from a thread other than the bridge's owning thread";
        return;
    }
    QDBusMessage msg = QDBusMessage::createMethodCall(m_endpoint.service, m_endpoint.objectPath, interfaceName, method);
    if (!args.isEmpty()) {
        msg.setArguments(args);
    }
    QDBusPendingCall pending = QDBusConnection::sessionBus().asyncCall(msg);
    // Without a watcher, async failures (service not present, method
    // not found, type mismatch) are completely swallowed. Attach one
    // that logs and deletes itself — keeps the "fire-and-forget"
    // ergonomics for callers while making real errors visible.
    //
    // Parent the watcher to the bridge so an in-flight call cancels cleanly
    // when the bridge is destroyed (Qt auto-deletes children). Without a
    // parent, a watcher whose owning thread's event loop ended before the
    // reply arrived would leak. asyncCall* are non-const because the
    // parent/child mutation is real state — const-correctness would lie.
    //
    // Soft-cap warning — under sustained load (the bridge issues calls
    // faster than the bus replies), the watcher queue grows
    // unbounded until either replies land or the bridge is destroyed.
    // 128 in-flight calls is well above any normal settings-app burst
    // (a save-all batch fans out ~10) but well below the point at
    // which the queue itself becomes a memory or scheduling pressure
    // problem. Fire once on first breach (>= cap, not just == cap, so
    // a burst that steps over the boundary still trips it) and latch
    // so a steady-state near-cap doesn't re-warn on every call.
    constexpr int PendingWatcherSoftCapWarn = 128;
    ++m_outstandingAsyncCalls;
    if (!m_softCapWarned && m_outstandingAsyncCalls >= PendingWatcherSoftCapWarn) {
        m_softCapWarned = true;
        qWarning() << "PhosphorControl::DBusBridge::asyncCallOn: pending-watcher count reached"
                   << m_outstandingAsyncCalls
                   << "— caller may be issuing async calls faster than the bus is replying;"
                      " consider awaiting replies before fanning out further.";
    }
    auto* watcher = new QDBusPendingCallWatcher(pending, this);
    QObject::connect(
        watcher, &QDBusPendingCallWatcher::finished, watcher,
        [interfaceName, method, this](QDBusPendingCallWatcher* w) {
            QDBusPendingReply<> reply = *w;
            if (reply.isError()) {
                qWarning() << "PhosphorControl::DBusBridge::asyncCallOn: D-Bus error on" << interfaceName << method
                           << "—" << reply.error().name() << reply.error().message();
            }
            // Decrement under the same single-threaded discipline as the
            // increment above (bridge is parented to its owning thread,
            // and the asyncCallOn entry guards against cross-thread
            // callers). Safe to capture `this` because the watcher is
            // parented to the bridge — when the bridge is destroyed,
            // Qt cancels and deletes the watcher before this lambda can
            // fire on a torn-down bridge.
            if (m_outstandingAsyncCalls > 0)
                --m_outstandingAsyncCalls;
            w->deleteLater();
        },
        // Qt::SingleShotConnection self-disconnects after first fire.
        // QDBusPendingCallWatcher::finished is contractually fire-
        // once, but defending in depth means a future Qt-internal
        // re-emit can't deref `w` post-deleteLater.
        Qt::SingleShotConnection);
}

} // namespace PhosphorControl
