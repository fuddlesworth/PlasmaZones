// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorServiceNotifications/phosphorservicenotifications_export.h>

#include <QDBusConnection>
#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantMap>

#include <memory>

namespace PhosphorServiceNotifications {

class Notification;

/**
 * @brief The session-bus server for `org.freedesktop.Notifications` (Desktop
 * Notifications Spec 1.3).
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
 * `Notify` decodes the hint set (including the `image-data` → `QImage` path)
 * into a typed `Notification`, allocates ids, and updates in place on
 * `replaces_id`. Notifications expire per their `expire_timeout` (Critical never
 * auto-expires), can be dismissed or have an action invoked, and are exposed as
 * a list through `NotificationModel`.
 */
class PHOSPHORSERVICENOTIFICATIONS_EXPORT NotificationServer : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool nameAcquired READ nameAcquired NOTIFY nameAcquiredChanged)
    Q_PROPERTY(int defaultExpireTimeout READ defaultExpireTimeout WRITE setDefaultExpireTimeout NOTIFY
                   defaultExpireTimeoutChanged)

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

    /// (Re)acquire the well-known name. The constructor calls this with
    /// @p replaceExisting false (inert when the name is taken). A daemon that
    /// wants to take over from another notification server (the CLI's `--replace`
    /// path) calls it with true, which succeeds when the current owner allows
    /// replacement. Returns `nameAcquired()`. A no-op once the name is held.
    bool acquireName(bool replaceExisting = false);

    /// The well-known name + object path defined by the spec.
    [[nodiscard]] static QString serviceName();
    [[nodiscard]] static QString objectPath();

    /// The live notifications, in ascending id order. Each is owned by this
    /// server; the list is a snapshot (pointers stay valid until the matching
    /// `NotificationClosed` fires). The `NotificationModel` (milestone 5) seeds
    /// itself from this and tracks `notificationAdded` / `NotificationClosed`.
    [[nodiscard]] QList<Notification*> notifications() const;

    /// Timeout (ms) applied to a `Notify` whose `expire_timeout` is -1 (the
    /// spec's "server decides"). Critical notifications ignore this and never
    /// auto-expire. Default 5000. Changing it affects only future notifications,
    /// not timers already armed.
    [[nodiscard]] int defaultExpireTimeout() const;
    void setDefaultExpireTimeout(int ms);

    /// The user dismissed @p id (close reason 2): for a shell/toast to call when
    /// the user swipes or clicks the notification away. Q_INVOKABLE (not a slot)
    /// so it is callable from QML/CLI but never exported on the bus.
    Q_INVOKABLE void dismissNotification(uint id);

    /// Invoke @p actionKey on @p id. When @p activationToken is non-empty it is
    /// announced via `ActivationToken(id, token)` first (XDG activation, so the
    /// target app can raise its window), then `ActionInvoked(id, key)`. A
    /// non-resident notification is closed afterwards (reason 2); a resident one
    /// stays open. Q_INVOKABLE, not a slot: callable from QML/CLI, never exported
    /// (the bluetooth Agent1 keeps its respond* methods off the bus the same way).
    Q_INVOKABLE void invokeAction(uint id, const QString& actionKey, const QString& activationToken = QString());

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
    /// A fresh notification (new id) was ingested and decoded. A `replaces_id`
    /// update does NOT re-emit this; it mutates the existing object in place and
    /// fires that object's `Notification::changed()` instead.
    void notificationAdded(Notification* notification);

    /// A notification was closed; @p reason is the spec close-reason code
    /// (1 expired, 2 dismissed, 3 closed by CloseNotification, 4 undefined).
    /// Auto-relayed to the bus by the adaptor. The matching object is deleted
    /// right after this fires, so consumers must drop their pointer here.
    void NotificationClosed(uint id, uint reason);
    /// The user invoked action @p actionKey on notification @p id. Auto-relayed
    /// to the bus by the adaptor.
    void ActionInvoked(uint id, const QString& actionKey);
    /// Carries the XDG activation token for @p id ahead of an `ActionInvoked`,
    /// so the activated app can raise its window. Auto-relayed by the adaptor.
    void ActivationToken(uint id, const QString& activationToken);

    void nameAcquiredChanged();
    void defaultExpireTimeoutChanged();

private:
    Q_DISABLE_COPY_MOVE(NotificationServer)

    /// Single close path. Removes @p id from the live set, cancels its expiry
    /// timer, emits `NotificationClosed(id, reason)`, and deletes the object.
    void closeInternal(uint id, uint reason);
    /// (Re)arm or cancel @p notification's expiry timer from its current
    /// `expireTimeout` + urgency, honouring `defaultExpireTimeout`.
    void armExpiry(Notification* notification);

    class Private;
    std::unique_ptr<Private> d;
};

} // namespace PhosphorServiceNotifications
