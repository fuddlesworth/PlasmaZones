// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorServices/StatusNotifierHost.h>

#include <PhosphorServices/StatusNotifierItem.h>

#include "dbustypes.h"
#include "statusnotifierwatcher.h"

#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusInterface>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusServiceWatcher>
#include <QDebug>
#include <QHash>
#include <QStringList>

namespace PhosphorServices {

namespace {
constexpr auto kWatcherService = "org.kde.StatusNotifierWatcher";
constexpr auto kWatcherPath = "/StatusNotifierWatcher";
constexpr auto kWatcherInterface = "org.kde.StatusNotifierWatcher";
} // namespace

class StatusNotifierHost::Private
{
public:
    explicit Private(StatusNotifierHost* q)
        : q(q)
    {
    }

    StatusNotifierHost* q;
    StatusNotifierWatcher* watcher = nullptr;
    QString hostServiceName; ///< "org.kde.StatusNotifierHost-1234"
    QDBusServiceWatcher* nameWatcher = nullptr;
    QHash<QString, StatusNotifierItem*> items; ///< canonical "service/path" → item

    void connectToWatcher();
    void registerHost();
    void seedExistingItems();
    void onItemRegistered(const QString& canonical);
    void onItemUnregistered(const QString& canonical);
};

void StatusNotifierHost::Private::connectToWatcher()
{
    // Spec dance: every shell tries to own the Watcher name. The
    // first one wins; the rest stay passive and route their items
    // through the winner. The winner ALSO runs the host so a
    // single-shell setup (the common case) needs only one process.
    watcher = new StatusNotifierWatcher(q);

    auto bus = QDBusConnection::sessionBus();

    // Subscribe to the Watcher's signals over the bus regardless of
    // whether we own the service: another process may be the
    // canonical Watcher, and we still need its ItemRegistered
    // signals.
    bus.connect(QString::fromLatin1(kWatcherService), QString::fromLatin1(kWatcherPath),
                QString::fromLatin1(kWatcherInterface), QStringLiteral("StatusNotifierItemRegistered"), q,
                SLOT(_q_remoteItemRegistered(QString)));
    bus.connect(QString::fromLatin1(kWatcherService), QString::fromLatin1(kWatcherPath),
                QString::fromLatin1(kWatcherInterface), QStringLiteral("StatusNotifierItemUnregistered"), q,
                SLOT(_q_remoteItemUnregistered(QString)));

    // If we own the Watcher, the in-process signals fire too. Wire
    // them up so we don't depend on the bus loopback round-trip.
    if (watcher->isServiceOwner()) {
        QObject::connect(watcher, &StatusNotifierWatcher::StatusNotifierItemRegistered, q, [this](const QString& c) {
            onItemRegistered(c);
        });
        QObject::connect(watcher, &StatusNotifierWatcher::StatusNotifierItemUnregistered, q, [this](const QString& c) {
            onItemUnregistered(c);
        });
    }
}

void StatusNotifierHost::Private::registerHost()
{
    auto bus = QDBusConnection::sessionBus();
    hostServiceName = QStringLiteral("org.kde.StatusNotifierHost-%1").arg(QCoreApplication::applicationPid());
    bus.interface()->registerService(hostServiceName, QDBusConnectionInterface::DontQueueService);

    // Tell whichever process owns the Watcher service that we're a
    // host. Async — if the Watcher isn't up yet, the NameOwnerChanged
    // wire (below) will retry once it appears.
    QDBusInterface watcherIface(QString::fromLatin1(kWatcherService), QString::fromLatin1(kWatcherPath),
                                QString::fromLatin1(kWatcherInterface), bus);
    if (watcherIface.isValid()) {
        watcherIface.asyncCall(QStringLiteral("RegisterStatusNotifierHost"), hostServiceName);
        seedExistingItems();
    }
}

void StatusNotifierHost::Private::seedExistingItems()
{
    // Read the property — items that registered before we started
    // need to be backfilled. Async to keep the constructor cheap.
    auto bus = QDBusConnection::sessionBus();
    QDBusMessage msg =
        QDBusMessage::createMethodCall(QString::fromLatin1(kWatcherService), QString::fromLatin1(kWatcherPath),
                                       QStringLiteral("org.freedesktop.DBus.Properties"), QStringLiteral("Get"));
    msg << QString::fromLatin1(kWatcherInterface) << QStringLiteral("RegisteredStatusNotifierItems");
    auto pending = bus.asyncCall(msg);
    auto* watcher = new QDBusPendingCallWatcher(pending, q);
    QObject::connect(watcher, &QDBusPendingCallWatcher::finished, q, [this, watcher] {
        watcher->deleteLater();
        QDBusPendingReply<QVariant> reply = *watcher;
        if (reply.isError()) {
            return;
        }
        const auto list = reply.value().toStringList();
        for (const auto& canonical : list) {
            onItemRegistered(canonical);
        }
    });
}

void StatusNotifierHost::Private::onItemRegistered(const QString& canonical)
{
    if (items.contains(canonical))
        return;
    // Split canonical "service/path" back into (service, path).
    // Service starts with ':' (unique name) or 'o.' (well-known);
    // path starts with '/'.
    const int slash = canonical.indexOf(QLatin1Char('/'));
    if (slash < 0)
        return;
    const QString service = canonical.left(slash);
    const QString path = canonical.mid(slash);

    auto* item = new StatusNotifierItem(service, path, q);
    items.insert(canonical, item);
    Q_EMIT q->itemAdded(item);
    Q_EMIT q->itemCountChanged();
}

void StatusNotifierHost::Private::onItemUnregistered(const QString& canonical)
{
    auto* item = items.take(canonical);
    if (!item)
        return;
    Q_EMIT q->itemRemoved(item);
    Q_EMIT q->itemCountChanged();
    item->deleteLater();
}

StatusNotifierHost::StatusNotifierHost(QObject* parent)
    : QObject(parent)
    , d(new Private(this))
{
    registerDBusTypes();
    d->connectToWatcher();
    d->registerHost();

    // Re-register if the Watcher comes back later (e.g., it crashed
    // and respawned, or we started before any host).
    d->nameWatcher = new QDBusServiceWatcher(QString::fromLatin1(kWatcherService), QDBusConnection::sessionBus(),
                                             QDBusServiceWatcher::WatchForRegistration, this);
    connect(d->nameWatcher, &QDBusServiceWatcher::serviceRegistered, this, [this](const QString&) {
        d->registerHost();
    });
}

StatusNotifierHost::~StatusNotifierHost()
{
    delete d;
}

QList<StatusNotifierItem*> StatusNotifierHost::items() const
{
    return d->items.values();
}

int StatusNotifierHost::itemCount() const
{
    return d->items.size();
}

StatusNotifierItem* StatusNotifierHost::itemAt(int index) const
{
    if (index < 0 || index >= d->items.size())
        return nullptr;
    auto values = d->items.values();
    return values.value(index);
}

void StatusNotifierHost::_q_remoteItemRegistered(const QString& canonical)
{
    d->onItemRegistered(canonical);
}

void StatusNotifierHost::_q_remoteItemUnregistered(const QString& canonical)
{
    d->onItemUnregistered(canonical);
}

} // namespace PhosphorServices
