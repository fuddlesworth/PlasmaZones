// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include <QObject>
#include <QString>
#include <QVariantList>
#include <QtDBus/QDBusMessage>

#include "phosphorcontrol_export.h"

namespace PhosphorControl {

/**
 * Default bound for synchronous DBusBridge::call(): "definitely something
 * is wrong" rather than expected latency for in-memory daemon hash lookups.
 * Centralised so the struct default and the clamp warning agree.
 */
inline constexpr int DefaultSyncTimeoutMs = 500;

/**
 * Configured endpoint for a single D-Bus service the settings app talks to.
 *
 *   service       — bus name (e.g. "org.phosphor.plasmazones.daemon")
 *   objectPath    — object path on that service (e.g. "/Daemon")
 *   interfaceName — default interface used by call()/asyncCall(). Apps that
 *                   talk to multiple interfaces on the same object call
 *                   callOn() / asyncCallOn() with an explicit interface.
 *   syncTimeoutMs — bound for synchronous calls; see DefaultSyncTimeoutMs.
 *
 * The struct is a POD passed across the lib boundary by value, so it is
 * exported for visibility-hidden builds — downstream consumers need typeinfo
 * symbols to construct, copy, and pass DBusEndpoint instances cleanly.
 *
 * The `interface` member is named `interfaceName` rather than `interface`
 * because Windows MSVC `<objbase.h>` defines `interface` as a `struct`
 * macro, which collides with field-access syntax. Renaming the field is
 * less fragile than `#undef interface` in a public header (which would
 * pollute every consumer's translation unit downstream of the include).
 */
struct PHOSPHORCONTROL_EXPORT DBusEndpoint
{
    QString service;
    QString objectPath;
    QString interfaceName;
    int syncTimeoutMs = DefaultSyncTimeoutMs;
};

/**
 * Thin wrapper around QDBusConnection::sessionBus() that bakes in a
 * pre-configured endpoint. Replaces the ad-hoc pattern of repeating
 * service / path / interface constants at every call site.
 *
 * The bridge holds no state beyond its endpoint and is safe to construct
 * once per settings app and pass to whatever pages need daemon access.
 *
 * Signal subscription (QDBusConnection::sessionBus().connect(...)) is not
 * wrapped — D-Bus signals require string-based slot signatures that Qt's
 * member-function-pointer overloads cannot synthesise, and the current
 * settings-app callers prefer to keep that one-off plumbing local to the
 * subscribing class. Add a typed subscribe() helper here if a future
 * consumer needs symmetric subscribe/unsubscribe at the bridge level.
 */
class PHOSPHORCONTROL_EXPORT DBusBridge : public QObject
{
    Q_OBJECT

public:
    explicit DBusBridge(DBusEndpoint endpoint, QObject* parent = nullptr);
    ~DBusBridge() override;

    DBusEndpoint endpoint() const;

    /** Synchronous call on the endpoint's default interface, bounded by
     *  syncTimeoutMs. Returns the reply (which may be an error message). */
    QDBusMessage call(const QString& method, const QVariantList& args = {}) const;

    /** Synchronous call on a specified interface (for services with
     *  multiple interfaces on the same object path). */
    QDBusMessage callOn(const QString& interfaceName, const QString& method, const QVariantList& args = {}) const;

    /** Fire-and-forget call on the endpoint's default interface.
     *  Non-const: the implementation parents a QDBusPendingCallWatcher
     *  to `this` so the QObject child tree mutates. */
    void asyncCall(const QString& method, const QVariantList& args = {});

    /** Fire-and-forget call on a specified interface.
     *  Non-const for the same parenting rationale as asyncCall. */
    void asyncCallOn(const QString& interfaceName, const QString& method, const QVariantList& args = {});

private:
    DBusEndpoint m_endpoint;
    // Tracks in-flight QDBusPendingCallWatcher children so the soft-cap
    // warning is O(1) and burst-safe. The previous shape did
    // `findChildren<...>().size()` on every asyncCallOn (O(N) child
    // scan on the hot path), and the breach check fired only on the
    // exact transition count == cap, which two near-simultaneous calls
    // could step over without ever satisfying. Incrementing under each
    // watcher's creation + decrementing in its `finished` lambda
    // (single-threaded; bridge is parented to the owning thread)
    // keeps the count accurate without the scan.
    int m_outstandingAsyncCalls = 0;
    // One-shot latch so the soft-cap warning fires exactly once per
    // process even if the queue oscillates around the cap. Without
    // this, a steady-state count near 128 would re-warn every time
    // the count crossed back up to the cap.
    bool m_softCapWarned = false;
};

} // namespace PhosphorControl
