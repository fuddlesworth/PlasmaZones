// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorServiceNotifications/phosphorservicenotifications_export.h>

#include <QDBusConnection>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantMap>

#include <memory>

namespace PhosphorServiceNotifications {

/**
 * @brief The session-bus server for `org.freedesktop.Notifications` (Desktop
 * Notifications Spec 1.2).
 *
 * This library OWNS the well-known name `org.freedesktop.Notifications` and
 * answers it: it is the notification daemon, not a client of one. Because
 * exactly one process may own the name, a notification daemon already running
 * (dunst / mako / Plasma) is a hard conflict, surfaced as `nameAcquired() ==
 * false` rather than a degraded backend. The server then stays inert.
 *
 * The four spec methods (`Notify`, `CloseNotification`, `GetCapabilities`,
 * `GetServerInformation`) are exported through a generated adaptor
 * (`qt6_add_dbus_adaptor`); this object is the adaptor's forwarding target.
 * They all reply synchronously, which is why this is a generated adaptor and
 * not a direct `ExportAllSlots` object: the bluetooth `Agent1` had to hand-roll
 * dispatch only because its callbacks defer their reply, which is not the case
 * here. The spec signals (`NotificationClosed`, `ActionInvoked`) are declared
 * on this object and auto-relayed to the bus by the adaptor.
 *
 * Milestone 1 lands the plumbing: name acquisition, the static
 * `GetServerInformation` / `GetCapabilities`, and id allocation. Hint decode
 * (including the `image-data` → `QImage` path), the typed `Notification`
 * object + model, and expiry timers land in milestones 3-5.
 */
class PHOSPHORSERVICENOTIFICATIONS_EXPORT NotificationServer : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool nameAcquired READ nameAcquired NOTIFY nameAcquiredChanged)

public:
    /// Construct on the session bus under the spec's well-known name.
    explicit NotificationServer(QObject* parent = nullptr);
    /// Dependency-injected ctor (tests drive a private peer-to-peer bus, so the
    /// whole ingest path runs with no real session daemon and no name conflict).
    NotificationServer(QDBusConnection connection, QString service, QObject* parent = nullptr);
    ~NotificationServer() override;

    /// True once this process owns the well-known name and is the active
    /// notification server. False when another daemon holds it (see class docs).
    [[nodiscard]] bool nameAcquired() const;

    /// The well-known name + object path defined by the spec.
    [[nodiscard]] static QString serviceName();
    [[nodiscard]] static QString objectPath();

public Q_SLOTS:
    // org.freedesktop.Notifications, forwarded here by the generated adaptor.
    // Signatures mirror the spec exactly; the PascalCase names are spec-dictated
    // (as with the bluetooth Agent1 slots) and intentionally break the project's
    // action-verb slot-naming rule. All reply synchronously.
    uint Notify(const QString& appName, uint replacesId, const QString& appIcon, const QString& summary,
                const QString& body, const QStringList& actions, const QVariantMap& hints, int expireTimeout);
    void CloseNotification(uint id);
    [[nodiscard]] QStringList GetCapabilities();
    QString GetServerInformation(QString& vendor, QString& version, QString& specVersion);

Q_SIGNALS:
    /// A notification was closed; @p reason is the spec close-reason code
    /// (1 expired, 2 dismissed, 3 closed by CloseNotification, 4 undefined).
    /// Auto-relayed to the bus by the adaptor.
    void NotificationClosed(uint id, uint reason);
    /// The user invoked action @p actionKey on notification @p id. Auto-relayed
    /// to the bus by the adaptor.
    void ActionInvoked(uint id, const QString& actionKey);

    void nameAcquiredChanged();

private:
    Q_DISABLE_COPY_MOVE(NotificationServer)
    class Private;
    std::unique_ptr<Private> d;
};

} // namespace PhosphorServiceNotifications
