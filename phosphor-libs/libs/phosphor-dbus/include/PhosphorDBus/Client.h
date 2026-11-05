// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorDBus/phosphordbus_export.h>

#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCall>
#include <QString>
#include <QVariantList>

class QObject;
class QLoggingCategory;

namespace PhosphorDBus {

/**
 * @brief A lightweight, service-agnostic D-Bus method-call client.
 *
 * `Client` is a plain value object — copyable, cheap to construct, holding no
 * shared global state. It binds a `(connection, service, objectPath)` triple
 * so callers issue method calls without re-typing the destination on every
 * invocation. Each consumer constructs its own; there is no singleton.
 *
 * All calls use `QDBusMessage::createMethodCall` directly rather than
 * `QDBusInterface`, so no synchronous wire introspection ever blocks the
 * calling thread (important for compositor plugins).
 *
 * To talk to a different service, construct another `Client`. A project that
 * always targets one daemon typically wraps construction in a small factory
 * (e.g. `PhosphorProtocol::daemonClient()`).
 */
class PHOSPHORDBUS_EXPORT Client
{
public:
    /**
     * @param connection  Bus connection to issue calls on (e.g. session bus).
     * @param service     Destination D-Bus service name.
     * @param objectPath  Destination object path.
     * @param log         Logging category for call-failure warnings; when
     *                    null, `lcPhosphorDBus()` is used. Must have static /
     *                    program lifetime: `fireAndForget`'s async callback
     *                    dereferences it after the `Client` itself may be
     *                    gone. Categories declared with `Q_LOGGING_CATEGORY`
     *                    satisfy this; a stack-allocated category does not.
     */
    Client(QDBusConnection connection, QString service, QString objectPath, const QLoggingCategory* log = nullptr);

    QDBusConnection connection() const
    {
        return m_connection;
    }
    QString service() const
    {
        return m_service;
    }
    QString objectPath() const
    {
        return m_objectPath;
    }

    /**
     * @brief Fire-and-forget async call with error logging.
     *
     * Allocates a `QDBusPendingCallWatcher` parented to @p parent so the call
     * is cancelled if the parent is destroyed first. The reply is discarded;
     * only errors are logged.
     *
     * @param parent      QObject parent for the watcher (must be non-null).
     * @param interface   D-Bus interface name.
     * @param method      Method name.
     * @param args        Method arguments.
     * @param logContext  Human-readable label for the warning log.
     */
    void fireAndForget(QObject* parent, const QString& interface, const QString& method, const QVariantList& args,
                       const QString& logContext = {}) const;

    /**
     * @brief Send a one-way notification with no expected reply.
     *
     * Uses `QDBusConnection::send`, so no watcher is allocated and no reply
     * timeout applies. Use for genuine notifications where allocating a
     * watcher just to ignore the reply would be wasted state.
     */
    void sendOneWay(const QString& interface, const QString& method, const QVariantList& args = {}) const;

    /**
     * @brief Issue an async method call and return the pending result.
     *
     * The caller attaches its own `QDBusPendingCallWatcher` to consume the
     * reply.
     */
    QDBusPendingCall asyncCall(const QString& interface, const QString& method, const QVariantList& args = {}) const;

    /**
     * @brief Blocking method call with an explicit timeout.
     *
     * For the rare paths that legitimately need a synchronous reply. Prefer
     * @ref asyncCall whenever the caller can tolerate a callback.
     *
     * @param timeoutMs  Reply timeout; pass a negative value for the QtDBus
     *                   default. Returns a message of type `ErrorMessage` on
     *                   failure or timeout.
     */
    QDBusMessage syncCall(const QString& interface, const QString& method, const QVariantList& args = {},
                          int timeoutMs = -1) const;

    /**
     * @brief Build (but do not send) a method-call message for this target.
     *
     * Exposed for callers that need the raw `QDBusMessage` — e.g. to attach a
     * bespoke watcher or batch sends.
     */
    QDBusMessage createCall(const QString& interface, const QString& method, const QVariantList& args = {}) const;

private:
    QDBusConnection m_connection;
    QString m_service;
    QString m_objectPath;
    const QLoggingCategory* m_log;
};

} // namespace PhosphorDBus
