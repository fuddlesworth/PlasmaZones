// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorServiceNotifications/NotificationServer.h>

#include "notificationsadaptor.h"

#include <QLoggingCategory>
#include <QSet>

namespace {
constexpr auto kServiceName = "org.freedesktop.Notifications";
constexpr auto kObjectPath = "/org/freedesktop/Notifications";

// GetServerInformation fields. The version tracks the library SOVERSION line;
// the spec version is the level this server implements.
constexpr auto kServerName = "Phosphor";
constexpr auto kServerVendor = "phosphor-works";
constexpr auto kServerVersion = "0.1.0";
constexpr auto kSpecVersion = "1.2";

// Spec close-reason codes (org.freedesktop.Notifications NotificationClosed).
constexpr uint kReasonClosedByCall = 3;

Q_LOGGING_CATEGORY(lcNotificationServer, "phosphor.service.notifications")
} // namespace

namespace PhosphorServiceNotifications {

class NotificationServer::Private
{
public:
    QDBusConnection connection;
    QString service;
    bool nameAcquired = false;

    // Monotonic, non-zero id allocator. The spec requires Notify to return a
    // non-zero id; 0 is reserved (replaces_id == 0 means "allocate a new one").
    uint nextId = 0;
    // Live notification ids. Milestone 1 tracks only the id so CloseNotification
    // and replaces_id stay coherent; the typed Notification records (content,
    // hints, decoded image, expiry) replace this set in milestone 3.
    QSet<uint> liveIds;

    explicit Private(QDBusConnection conn, QString svc)
        : connection(std::move(conn))
        , service(std::move(svc))
    {
    }

    uint allocateId()
    {
        return ++nextId;
    }
};

NotificationServer::NotificationServer(QObject* parent)
    : NotificationServer(QDBusConnection::sessionBus(), serviceName(), parent)
{
}

NotificationServer::NotificationServer(QDBusConnection connection, QString service, QObject* parent)
    : QObject(parent)
    , d(std::make_unique<Private>(std::move(connection), std::move(service)))
{
    // The adaptor forwards the four spec methods to this object's slots and
    // auto-relays its NotificationClosed / ActionInvoked signals. It is parented
    // to this object, so its lifetime is ours.
    new NotificationsAdaptor(this);

    if (!d->connection.registerObject(objectPath(), this, QDBusConnection::ExportAdaptors)) {
        qCWarning(lcNotificationServer) << "failed to export the notifications object at" << objectPath();
        return;
    }

    // Acquire the well-known name. Exactly one process may own it, so this fails
    // when another daemon (dunst / mako / Plasma) already holds it; we then stay
    // inert (nameAcquired == false) rather than fighting for the name. Taking
    // over from another daemon is an explicit opt-in exposed by the CLI demo
    // (milestone 7), not a default here.
    d->nameAcquired = d->connection.registerService(d->service);
    if (!d->nameAcquired) {
        qCInfo(lcNotificationServer) << "another notification daemon owns" << d->service << "- staying inert";
    }
}

NotificationServer::~NotificationServer()
{
    if (d->nameAcquired)
        d->connection.unregisterService(d->service);
    d->connection.unregisterObject(objectPath());
}

bool NotificationServer::nameAcquired() const
{
    return d->nameAcquired;
}

QString NotificationServer::serviceName()
{
    return QLatin1String(kServiceName);
}

QString NotificationServer::objectPath()
{
    return QLatin1String(kObjectPath);
}

uint NotificationServer::Notify(const QString& appName, uint replacesId, const QString& appIcon, const QString& summary,
                                const QString& body, const QStringList& actions, const QVariantMap& hints,
                                int expireTimeout)
{
    // Milestone 1: allocate (or honour replaces_id) and acknowledge. Hint decode,
    // content storage, the typed Notification + model, and expiry timers land in
    // milestones 3-5; the unused parameters below are consumed there.
    Q_UNUSED(appName)
    Q_UNUSED(appIcon)
    Q_UNUSED(summary)
    Q_UNUSED(body)
    Q_UNUSED(actions)
    Q_UNUSED(hints)
    Q_UNUSED(expireTimeout)

    const bool reuse = replacesId != 0 && d->liveIds.contains(replacesId);
    const uint id = reuse ? replacesId : d->allocateId();
    d->liveIds.insert(id);
    return id;
}

void NotificationServer::CloseNotification(uint id)
{
    // A close for an unknown id is a no-op (the notification already expired or
    // was dismissed). For a live one, drop it and announce the reason.
    if (d->liveIds.remove(id))
        Q_EMIT NotificationClosed(id, kReasonClosedByCall);
}

QStringList NotificationServer::GetCapabilities()
{
    // Advertise only what the server can honestly back today. The set grows as
    // capabilities land: "actions" (milestone 4), "body-markup" / "body-images"
    // / "icon-static" (decode, milestone 3), "persistence" (model, milestone 5).
    return {QStringLiteral("body")};
}

QString NotificationServer::GetServerInformation(QString& vendor, QString& version, QString& specVersion)
{
    vendor = QLatin1String(kServerVendor);
    version = QLatin1String(kServerVersion);
    specVersion = QLatin1String(kSpecVersion);
    return QLatin1String(kServerName);
}

} // namespace PhosphorServiceNotifications
