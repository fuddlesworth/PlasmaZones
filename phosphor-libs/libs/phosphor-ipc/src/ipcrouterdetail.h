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
// ipcrouterdispatch.cpp. Holds the meta-helpers, so both translation
// units agree on the introspection floor (QObject's built-in methods,
// dropped from the wire-callable surface) and the access filter (Public
// only), and the router's per-connection resource caps, kept together
// so the limits a client can run into are listed in one place (the
// deferred-call cap, which targets document, is public in IpcRouter.h).
// Not installed; consumers see only the public IpcRouter API.
namespace PhosphorIpc::detail {

// Per-socket subscription cap. Bounds the amplification factor a
// single client can pin on broadcastEvent.
inline constexpr int MaxSubscriptionsPerSocket = 256;

// Hard cap on a single NDJSON request line. The router buffers
// unframed bytes until canReadLine() returns true; without a cap,
// a peer that never sends '\n' would let the per-socket buffer
// grow until the process OOMs. 1 MiB is far above any realistic
// shell request (the upper-bound observed is ~few hundred bytes
// of args+target+fn payload) and well under any kernel send/recv
// buffer ceiling.
inline constexpr qint64 MaxLineBytes = 1024 * 1024;

// Cap on frames dispatched per batch of IpcRouter::readFrames. A peer
// that pipelines thousands of frames in a single burst would otherwise
// let the dispatch loop monopolise the event loop and starve other
// connections. When the cap is hit and more lines remain, the router
// schedules the next batch via a queued invocation, yielding to the
// event loop between batches.
inline constexpr int MaxFramesPerReadyRead = 64;

// Maximum number of CONSECUTIVE malformed frames a peer may send
// before the router force-closes the connection. The (N+1)-th
// consecutive malformed frame triggers the close (i.e. the cap is
// "the largest run we tolerate"). A well-formed frame anywhere in
// the stream resets the counter; we only close on sustained
// garbage, not on a one-off bad line.
inline constexpr int MaxConsecutiveMalformedFrames = 16;

// Write-side cap on the events queued to one connection, symmetric to
// the read-side line cap (MaxLineBytes). A client that never
// reads from its socket would otherwise let the router's pending-write
// queue for it grow without bound as events pile up. 16 MiB, 16x the
// read cap, because event payloads can legitimately be larger than
// request lines and a slow consumer is more common than a malformed
// client. broadcastEvent and sendEvent check it before each event and
// close a connection that is further behind. Replies are not checked:
// there is one per request, so the client's own requests bound them.
inline constexpr qint64 MaxBytesToWrite = 16 * 1024 * 1024;

// The cap on deferred calls per connection is public, since a target's
// client can run into it: IpcRouter::MaxPendingCallsPerConnection.

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
