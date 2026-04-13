// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "plasmazones_export.h"

#include <QLatin1String>
#include <QPointer>
#include <QString>
#include <QVariantList>

class QObject;

namespace PlasmaZones {

/**
 * @brief Identifiers for a single-instance D-Bus service.
 *
 * Bundles the well-known name, object path, and interface name so call sites
 * pass a single constant instead of three loose strings that could drift.
 */
struct SingleInstanceIds
{
    QLatin1String serviceName;
    QLatin1String objectPath;
    QLatin1String interfaceName;
};

/**
 * @brief RAII owner of a single-instance D-Bus service registration.
 *
 * Claims a well-known name on the session bus and exports a QObject at the
 * matching path with `QDBusConnection::ExportScriptableSlots`. Releases both
 * on destruction.
 *
 * Intended to be owned by the QObject it exports (e.g. an EditorController
 * or SettingsController); declaration order places the member dtor before
 * the QObject base dtor, so the D-Bus export is cleanly torn down while the
 * owner is still a valid QObject.
 *
 * Also provides static helpers for the mirror-image operation: forwarding a
 * launch request to an already-running instance. Both the claim and the
 * forward paths share a single `isServiceRegistered` pre-check so callers
 * cannot accidentally trigger D-Bus auto-activation.
 */
class PLASMAZONES_EXPORT SingleInstanceService
{
public:
    /// Bind identifiers to the object that will be exported. Does not talk to
    /// the bus yet — call `claim()` from the owner at the correct point in
    /// its lifecycle (typically before any heavy startup work).
    SingleInstanceService(SingleInstanceIds ids, QObject* exportObject);
    ~SingleInstanceService();

    SingleInstanceService(const SingleInstanceService&) = delete;
    SingleInstanceService& operator=(const SingleInstanceService&) = delete;

    /**
     * @brief Claim the well-known name and export the object.
     * @return true if the name and object path were both successfully registered.
     *
     * Releases the name again if object registration fails, so the bus never
     * ends up holding a name with no reachable object (which would cause
     * forwarders to spin forever).
     */
    bool claim();

    /// True once `claim()` has succeeded and the registration is still live.
    bool isClaimed() const
    {
        return m_claimed;
    }

    /**
     * @brief Cheap check via the bus daemon for whether the service name has
     * a current owner. Uses `org.freedesktop.DBus.NameHasOwner` — does not
     * trigger D-Bus activation even if a `.service` file exists.
     */
    static bool isRunning(const SingleInstanceIds& ids);

    /**
     * @brief Forward a method call to the running instance.
     *
     * Performs the full check-then-call sequence: verify the name has an
     * owner, build a QDBusInterface, apply the timeout, and invoke the
     * method with the provided arguments.
     *
     * @return true only if a `ReplyMessage` came back. Any other outcome
     * (no running instance, invalid proxy, timeout, error) yields false.
     */
    static bool forward(const SingleInstanceIds& ids, const QString& method, const QVariantList& args,
                        int timeoutMs = 3000);

private:
    SingleInstanceIds m_ids;
    QPointer<QObject> m_exportObject;
    bool m_claimed = false;
};

} // namespace PlasmaZones
