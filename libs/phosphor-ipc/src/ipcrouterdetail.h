// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include <QByteArray>
#include <QMetaMethod>
#include <QMetaObject>
#include <QObject>
#include <QString>

// Private detail header shared between ipcrouter.cpp and
// ipcrouterdispatch.cpp. Holds the meta-helpers and the per-router
// resource caps so both translation units agree on the introspection
// floor (QObject's built-in methods, dropped from the wire-callable
// surface), the access filter (Public only), and the cap constants.
// Not installed; consumers see only the public IpcRouter API.
namespace PhosphorIpc::detail {

// Per-socket subscription cap. Bounds the amplification factor a
// single client can pin on broadcastEvent. Lives in the shared
// header because both the read-side dispatcher (handleSubscribe)
// and any future router-side accounting need the same value.
inline constexpr int MaxSubscriptionsPerSocket = 256;

// First method index above QObject's built-ins. Filters destroyed,
// objectNameChanged, deleteLater out of the wire surface while
// keeping every user-declared method (including those inherited
// from a non-QObject base above QObject's methodCount).
[[nodiscard]] inline int qobjectMethodCount()
{
    return QObject::staticMetaObject.methodCount();
}

// Find a Q_INVOKABLE method by name AND arity. Overload resolution
// favours an exact arity match; when no overload's parameter count
// lines up, returns the first name-matching method so the caller
// can surface a precise ArgCountMismatch citing the metamethod's
// expected arity. Filters to Public access — protected Q_INVOKABLE
// methods are a "subclass-only" marker by convention and must not
// reach the wire.
[[nodiscard]] inline QMetaMethod findInvokableMethod(const QMetaObject* meta, const QString& fnName, int expectedArity)
{
    if (!meta) {
        return {};
    }
    const QByteArray needle = fnName.toUtf8();
    QMetaMethod nameOnlyFallback;
    for (int i = qobjectMethodCount(); i < meta->methodCount(); ++i) {
        const QMetaMethod m = meta->method(i);
        if (m.methodType() == QMetaMethod::Signal) {
            continue;
        }
        if (m.access() != QMetaMethod::Public) {
            continue;
        }
        if (m.name() != needle) {
            continue;
        }
        if (m.parameterCount() == expectedArity) {
            return m;
        }
        if (!nameOnlyFallback.isValid()) {
            nameOnlyFallback = m;
        }
    }
    return nameOnlyFallback;
}

// Find a Public signal by name. The subscribe path uses this for
// existence validation (typos surface as NO_SUCH_SIGNAL at
// subscribe time instead of silently never delivering events).
// Same Public-access filter as findInvokableMethod so the schema
// and the dispatcher advertise the same set of subscribable signals.
[[nodiscard]] inline QMetaMethod findSignal(const QMetaObject* meta, const QString& signalName)
{
    if (!meta) {
        return {};
    }
    const QByteArray needle = signalName.toUtf8();
    for (int i = qobjectMethodCount(); i < meta->methodCount(); ++i) {
        const QMetaMethod m = meta->method(i);
        if (m.methodType() != QMetaMethod::Signal) {
            continue;
        }
        if (m.access() != QMetaMethod::Public) {
            continue;
        }
        if (m.name() == needle) {
            return m;
        }
    }
    return {};
}

} // namespace PhosphorIpc::detail
