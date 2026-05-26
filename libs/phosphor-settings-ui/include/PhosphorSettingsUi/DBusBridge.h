// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include <QObject>
#include <QString>
#include <QVariantList>
#include <QtDBus/QDBusMessage>

#include "phosphorsettingsui_export.h"

namespace PhosphorSettingsUi {

/**
 * Configured endpoint for a single D-Bus service the settings app talks to.
 *
 *   service       — bus name (e.g. "org.phosphor.plasmazones.daemon")
 *   objectPath    — object path on that service (e.g. "/Daemon")
 *   interface     — default interface used by call()/asyncCall(). Apps that
 *                   talk to multiple interfaces on the same object call
 *                   callOn() / asyncCallOn() with an explicit interface.
 *   syncTimeoutMs — bound for synchronous calls. The default of 500 ms is
 *                   "definitely something is wrong" rather than expected
 *                   latency for in-memory daemon hash lookups.
 */
struct DBusEndpoint
{
    QString service;
    QString objectPath;
    QString interface;
    int syncTimeoutMs = 500;
};

/**
 * Thin wrapper around QDBusConnection::sessionBus() that bakes in a
 * pre-configured endpoint. Replaces the ad-hoc dbusutils.h pattern where
 * every call site had to repeat the service / path / interface constants.
 *
 * The bridge holds no state beyond its endpoint and is safe to construct
 * once per settings app and pass to whatever pages need daemon access.
 */
class PHOSPHORSETTINGSUI_EXPORT DBusBridge : public QObject
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
    QDBusMessage callOn(const QString& interface, const QString& method, const QVariantList& args = {}) const;

    /** Fire-and-forget call on the endpoint's default interface. */
    void asyncCall(const QString& method, const QVariantList& args = {}) const;

    /** Fire-and-forget call on a specified interface. */
    void asyncCallOn(const QString& interface, const QString& method, const QVariantList& args = {}) const;

private:
    DBusEndpoint m_endpoint;
};

} // namespace PhosphorSettingsUi
