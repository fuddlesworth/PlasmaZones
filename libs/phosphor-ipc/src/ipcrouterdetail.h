// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include <PhosphorIpc/IpcTarget.h>

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

// First method index that counts as user-declared on `object`.
// For a plain QObject-derived target the floor is QObject's own
// methodCount (filters destroyed, objectNameChanged, deleteLater,
// etc.). For QML-instantiated IpcTarget wrappers (the common case
// the QML import surface produces), the floor is IpcTarget's
// methodCount so the wrapper's own infrastructure methods —
// notably IpcTarget::emitEvent, a Q_INVOKABLE called by QML to
// inject events into the router — do NOT become wire-callable on
// every IpcTarget instance. The schema generator and the
// dispatcher (findInvokableMethod / findSignal) both call this
// helper so they advertise and dispatch the SAME set of methods,
// preventing wire/schema drift that would let a remote client
// invoke methods the schema never advertised.
[[nodiscard]] inline int firstUserMethodIndex(const QObject* object)
{
    if (qobject_cast<const IpcTarget*>(object)) {
        return IpcTarget::staticMetaObject.methodCount();
    }
    return QObject::staticMetaObject.methodCount();
}

// Find a Q_INVOKABLE method by name AND arity. Overload resolution
// favours an exact arity match; when no overload's parameter count
// lines up, returns the first name-matching method so the caller
// can surface a precise ArgCountMismatch citing the metamethod's
// expected arity. Filters to Public access — protected Q_INVOKABLE
// methods are a "subclass-only" marker by convention and must not
// reach the wire.
[[nodiscard]] inline QMetaMethod findInvokableMethod(const QObject* object, const QString& fnName, int expectedArity)
{
    if (!object) {
        return {};
    }
    const QMetaObject* meta = object->metaObject();
    if (!meta) {
        return {};
    }
    const QByteArray needle = fnName.toUtf8();
    QMetaMethod nameOnlyFallback;
    for (int i = firstUserMethodIndex(object); i < meta->methodCount(); ++i) {
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
[[nodiscard]] inline QMetaMethod findSignal(const QObject* object, const QString& signalName)
{
    if (!object) {
        return {};
    }
    const QMetaObject* meta = object->metaObject();
    if (!meta) {
        return {};
    }
    const QByteArray needle = signalName.toUtf8();
    for (int i = firstUserMethodIndex(object); i < meta->methodCount(); ++i) {
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
