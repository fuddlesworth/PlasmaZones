// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorServiceNotifications/NotificationServer.h>

#include <PhosphorServiceNotifications/Notification.h>

#include "notificationsadaptor.h"

#include <PhosphorServiceIconTheme/IconThemeResolver.h>

#include <QDBusArgument>
#include <QDBusConnectionInterface>
#include <QDateTime>
#include <QHash>
#include <QLoggingCategory>
#include <QMap>
#include <QTimer>
#include <QUrl>

namespace {
constexpr auto kServiceName = "org.freedesktop.Notifications";
constexpr auto kObjectPath = "/org/freedesktop/Notifications";

// GetServerInformation fields. The version tracks the library VERSION; the spec
// version is the level this server implements.
constexpr auto kServerName = "Phosphor";
constexpr auto kServerVendor = "phosphor-works";
constexpr auto kServerVersion = "0.1.0";
// The org.freedesktop.Notifications spec level this server is compliant with.
// 1.3 (Aug 2024) is the current spec; the ActivationToken signal we implement
// is its defining addition over 1.2.
constexpr auto kSpecVersion = "1.3";

// Spec close-reason codes (org.freedesktop.Notifications NotificationClosed).
constexpr uint kReasonExpired = 1;
constexpr uint kReasonDismissed = 2;
constexpr uint kReasonClosedByCall = 3;

// Timeout (ms) for a Notify whose expire_timeout is -1 (server decides), unless
// the notification is Critical (which never auto-expires). Settable per server.
constexpr int kDefaultExpireMs = 5000;

// Default icon-resolution size for image-path-as-icon-name. The full-fidelity
// route is the image-data hint; a name resolves to a reasonable static size and
// the raw app_icon / hint strings stay available for a consumer that wants to
// re-resolve at its own scale.
constexpr int kIconResolveSize = 64;

Q_LOGGING_CATEGORY(lcNotificationServer, "phosphor.service.notifications")

// Decode the spec image-data struct `(iiibiiay)`: width, height, rowstride,
// has-alpha, bits-per-sample, channels, pixel bytes. Returns a deep-copied
// QImage (the byte array is owned by the QDBusArgument and goes out of scope),
// or a null image when the struct is malformed.
QImage decodeImageData(const QDBusArgument& arg)
{
    int width = 0;
    int height = 0;
    int rowstride = 0;
    bool hasAlpha = false;
    int bitsPerSample = 0;
    int channels = 0;
    QByteArray data;

    arg.beginStructure();
    arg >> width >> height >> rowstride >> hasAlpha >> bitsPerSample >> channels >> data;
    arg.endStructure();

    // width / height / rowstride / channels are attacker-controlled: any session
    // peer can call Notify. Bound the dimensions before any arithmetic. This both
    // prevents signed-int overflow in the size math below (an INT_MAX width times
    // channels wraps negative and would defeat the buffer check) and rejects
    // absurd allocations (an un-capped width*height makes QImage::copy a trivial
    // OOM DoS). Only the common 8-bit, 3- (RGB) or 4-channel (RGBA) layouts are
    // supported; anything else is rejected rather than risking a misread.
    constexpr int kMaxImageDimension = 8192;
    if (width <= 0 || height <= 0 || width > kMaxImageDimension || height > kMaxImageDimension || bitsPerSample != 8
        || (channels != 3 && channels != 4))
        return {};

    // All size math in qsizetype: width is bounded above but rowstride is not, so
    // the products must not wrap. A negative rowstride also fails this check.
    const qsizetype minRowBytes = static_cast<qsizetype>(width) * channels;
    if (rowstride < minRowBytes)
        return {};
    if (data.size() < static_cast<qsizetype>(rowstride) * (height - 1) + minRowBytes)
        return {};

    const QImage::Format format = channels == 4 ? QImage::Format_RGBA8888 : QImage::Format_RGB888;
    const QImage view(reinterpret_cast<const uchar*>(data.constData()), width, height, rowstride, format);
    return view.copy();
}

bool hintBool(const QVariantMap& hints, QLatin1String key)
{
    return hints.value(key).toBool();
}
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

    // Live notifications keyed by id, each parented to the server. A QMap keeps
    // notifications() in ascending-id order without a separate sort.
    QMap<uint, Notification*> live;
    // Per-notification single-shot expiry timers, parented to the server and
    // torn down with their notification. Absent when a notification never
    // expires (expire_timeout 0, or -1 on a Critical).
    QHash<uint, QTimer*> timers;

    int defaultExpireMs = kDefaultExpireMs;

    explicit Private(QDBusConnection conn, QString svc)
        : connection(std::move(conn))
        , service(std::move(svc))
    {
    }

    uint allocateId()
    {
        // Skip 0 (reserved: replaces_id == 0 means "new") and any id still live,
        // so a wrap past 2^32 ids cannot collide with an existing notification and
        // silently overwrite (leaking) it.
        do {
            ++nextId;
        } while (nextId == 0 || live.contains(nextId));
        return nextId;
    }

    // Resolve the rich notification image: image-data hint first (full fidelity),
    // then image-path (a file path / URI, or a freedesktop icon name).
    QImage decodeImage(const QVariantMap& hints)
    {
        for (const auto* key : {"image-data", "image_data", "icon_data"}) {
            const QVariant value = hints.value(QLatin1String(key));
            if (value.canConvert<QDBusArgument>()) {
                const QImage image = decodeImageData(qvariant_cast<QDBusArgument>(value));
                if (!image.isNull())
                    return image;
            }
        }

        for (const auto* key : {"image-path", "image_path"}) {
            const QString path = hints.value(QLatin1String(key)).toString();
            if (path.isEmpty())
                continue;

            QString local = path;
            if (local.startsWith(QLatin1String("file://")))
                local = QUrl(local).toLocalFile();
            if (local.startsWith(QLatin1Char('/'))) {
                QImage file;
                if (file.load(local))
                    return file;
                continue;
            }
            // IconThemeResolver is a process-wide singleton (the parsed theme
            // index is expensive to build and shared across the shell).
            const QImage themed =
                PhosphorServiceIconTheme::IconThemeResolver::instance()->iconForName(path, kIconResolveSize);
            if (!themed.isNull())
                return themed;
        }
        return {};
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
    // over from another daemon is an explicit opt-in (acquireName(true), the CLI
    // demo's --replace), not a default here.
    if (!acquireName(false)) {
        qCInfo(lcNotificationServer) << "another notification daemon owns" << d->service << "- staying inert";
    }
}

bool NotificationServer::acquireName(bool replaceExisting)
{
    if (d->nameAcquired)
        return true;

    QDBusConnectionInterface* iface = d->connection.interface();
    if (!iface) {
        // No bus daemon (peer-to-peer connection, e.g. tests): a plain attempt
        // is all that applies, and well-known names don't really exist there.
        d->nameAcquired = d->connection.registerService(d->service);
    } else {
        const QDBusConnectionInterface::RegisterServiceReply reply =
            iface->registerService(d->service,
                                   replaceExisting ? QDBusConnectionInterface::ReplaceExistingService
                                                   : QDBusConnectionInterface::DontQueueService,
                                   QDBusConnectionInterface::DontAllowReplacement);
        d->nameAcquired = reply == QDBusConnectionInterface::ServiceRegistered;
    }

    if (d->nameAcquired)
        Q_EMIT nameAcquiredChanged();
    return d->nameAcquired;
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

QList<Notification*> NotificationServer::notifications() const
{
    return d->live.values();
}

uint NotificationServer::Notify(const QString& appName, uint replacesId, const QString& appIcon, const QString& summary,
                                const QString& body, const QStringList& actions, const QVariantMap& hints,
                                int expireTimeout)
{
    const bool reuse = replacesId != 0 && d->live.contains(replacesId);
    const uint id = reuse ? replacesId : d->allocateId();

    Notification* notification = reuse ? d->live.value(id) : new Notification(id, this);

    // Direct field writes: NotificationServer is Notification's friend, so the
    // ingest logic lives here rather than in a setter-per-field surface.
    notification->m_appName = appName;
    notification->m_appIcon = appIcon;
    notification->m_summary = summary;
    notification->m_body = body;
    notification->m_actions = actions;
    notification->m_expireTimeout = expireTimeout;
    notification->m_hints = hints;

    // Spec default is Normal when the hint is absent; an explicit 0 means Low, so
    // "absent" must be distinguished from "0" (a bare toUInt() would conflate them).
    if (hints.contains(QLatin1String("urgency"))) {
        const uint urgency = hints.value(QLatin1String("urgency")).toUInt();
        notification->m_urgency =
            urgency <= Notification::Critical ? static_cast<Notification::Urgency>(urgency) : Notification::Normal;
    } else {
        notification->m_urgency = Notification::Normal;
    }
    notification->m_category = hints.value(QLatin1String("category")).toString();
    notification->m_desktopEntry = hints.value(QLatin1String("desktop-entry")).toString();
    notification->m_resident = hintBool(hints, QLatin1String("resident"));
    notification->m_transient = hintBool(hints, QLatin1String("transient"));
    notification->m_suppressSound = hintBool(hints, QLatin1String("suppress-sound"));
    notification->m_value = hints.contains(QLatin1String("value")) ? hints.value(QLatin1String("value")).toInt() : -1;
    notification->m_image = d->decodeImage(hints);

    if (reuse) {
        Q_EMIT notification->changed();
    } else {
        notification->m_timestamp = QDateTime::currentDateTime();
        d->live.insert(id, notification);
        Q_EMIT notificationAdded(notification);
    }

    // (Re)arm the expiry timer from the freshly-applied expire_timeout + urgency.
    // On a replace this restarts the countdown, matching the spec intent that a
    // replacing Notify fully re-specifies the notification.
    armExpiry(notification);
    return id;
}

void NotificationServer::CloseNotification(uint id)
{
    // The spec's explicit-close path. Unknown ids are a no-op (already expired
    // or dismissed).
    closeInternal(id, kReasonClosedByCall);
}

void NotificationServer::dismissNotification(uint id)
{
    closeInternal(id, kReasonDismissed);
}

void NotificationServer::invokeAction(uint id, const QString& actionKey, const QString& activationToken)
{
    Notification* notification = d->live.value(id);
    if (!notification)
        return;

    // XDG activation token (if any) goes out first so the target app can raise
    // its window as the action fires.
    if (!activationToken.isEmpty())
        Q_EMIT ActivationToken(id, activationToken);
    Q_EMIT ActionInvoked(id, actionKey);

    // Per spec, invoking an action dismisses a non-resident notification; a
    // resident one stays up so further actions can be taken.
    if (!notification->resident())
        closeInternal(id, kReasonDismissed);
}

void NotificationServer::closeInternal(uint id, uint reason)
{
    Notification* notification = d->live.take(id);
    if (!notification)
        return;
    if (QTimer* timer = d->timers.take(id)) {
        timer->stop();
        timer->deleteLater();
    }
    // Announce the close before deleting, so consumers (model, toast) can react
    // while the object is still readable; deleteLater defers the destruction.
    Q_EMIT NotificationClosed(id, reason);
    notification->deleteLater();
}

void NotificationServer::armExpiry(Notification* notification)
{
    const uint id = notification->id();

    int effective = notification->expireTimeout();
    if (effective < 0)
        effective = notification->urgency() == Notification::Critical ? 0 : d->defaultExpireMs;

    QTimer* timer = d->timers.value(id);

    // expire_timeout 0 (or a -1 Critical resolving to 0) means "never": tear down
    // any timer carried over from a previous Notify on this id.
    if (effective <= 0) {
        if (timer) {
            d->timers.remove(id);
            timer->stop();
            timer->deleteLater();
        }
        return;
    }

    if (!timer) {
        timer = new QTimer(this);
        timer->setSingleShot(true);
        connect(timer, &QTimer::timeout, this, [this, id] {
            closeInternal(id, kReasonExpired);
        });
        d->timers.insert(id, timer);
    }
    timer->start(effective);
}

int NotificationServer::defaultExpireTimeout() const
{
    return d->defaultExpireMs;
}

void NotificationServer::setDefaultExpireTimeout(int ms)
{
    if (d->defaultExpireMs == ms)
        return;
    d->defaultExpireMs = ms;
    Q_EMIT defaultExpireTimeoutChanged();
}

QStringList NotificationServer::GetCapabilities()
{
    // Advertise only what the server can honestly back today. "persistence" is
    // now backed: the server retains live notifications (NotificationModel
    // exposes them) until they are closed, rather than discarding on display.
    // "body-markup" stays out until a renderer exists (Phase 4.3).
    return {QStringLiteral("body"), QStringLiteral("actions"), QStringLiteral("icon-static"),
            QStringLiteral("persistence")};
}

QString NotificationServer::GetServerInformation(QString& vendor, QString& version, QString& specVersion)
{
    vendor = QLatin1String(kServerVendor);
    version = QLatin1String(kServerVersion);
    specVersion = QLatin1String(kSpecVersion);
    return QLatin1String(kServerName);
}

} // namespace PhosphorServiceNotifications
