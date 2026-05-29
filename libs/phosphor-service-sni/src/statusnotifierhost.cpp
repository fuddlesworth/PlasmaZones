// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorServiceSni/StatusNotifierHost.h>

#include <PhosphorServiceSni/StatusNotifierItem.h>

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
#include <QLoggingCategory>
#include <QStringList>

Q_LOGGING_CATEGORY(lcSniHost, "phosphor.service.sni")

namespace PhosphorServiceSni {

namespace {
// Inline helpers — each call returns a QStringLiteral-backed QString.
// CLAUDE.md forbids raw "..." with QString APIs; inline-function form
// keeps the call sites clean while honouring the rule.
inline QString kWatcherService()
{
    return QStringLiteral("org.kde.StatusNotifierWatcher");
}
inline QString kWatcherPath()
{
    return QStringLiteral("/StatusNotifierWatcher");
}
inline QString kWatcherInterface()
{
    return QStringLiteral("org.kde.StatusNotifierWatcher");
}
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

    // Items in registration order. The model maps row → item by
    // index, so the storage MUST be ordered — earlier rev used a
    // QHash here and `itemAt(N)` returned hash-bucket order, which
    // meant new items at "row count-1" weren't the items actually at
    // the end of the visible list, and the QML Repeater bound
    // delegates to the wrong items. Two containers: the list is the
    // ordered truth, the hash is an O(1) canonical-id lookup
    // (canonical is "service/path"). itemAdded/itemRemoved keep them
    // in lockstep.
    QList<StatusNotifierItem*> itemsList;
    QHash<QString, StatusNotifierItem*> itemsByCanonical;

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
    qCInfo(lcSniHost) << "watcher owner?" << watcher->isServiceOwner()
                      << "— if false, another shell (likely plasma) is the canonical watcher";

    auto bus = QDBusConnection::sessionBus();

    // Subscribe to the Watcher's signals over the bus regardless of
    // whether we own the service: another process may be the
    // canonical Watcher, and we still need its ItemRegistered
    // signals.
    bus.connect(kWatcherService(), kWatcherPath(), kWatcherInterface(), QStringLiteral("StatusNotifierItemRegistered"),
                q, SLOT(_q_remoteItemRegistered(QString)));
    bus.connect(kWatcherService(), kWatcherPath(), kWatcherInterface(),
                QStringLiteral("StatusNotifierItemUnregistered"), q, SLOT(_q_remoteItemUnregistered(QString)));

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
    QDBusInterface watcherIface(kWatcherService(), kWatcherPath(), kWatcherInterface(), bus);
    qCInfo(lcSniHost) << "host name registered:" << hostServiceName << "watcher iface valid?" << watcherIface.isValid();
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
    QDBusMessage msg = QDBusMessage::createMethodCall(
        kWatcherService(), kWatcherPath(), QStringLiteral("org.freedesktop.DBus.Properties"), QStringLiteral("Get"));
    msg << kWatcherInterface() << QStringLiteral("RegisteredStatusNotifierItems");
    auto pending = bus.asyncCall(msg);
    auto* watcher = new QDBusPendingCallWatcher(pending, q);
    QObject::connect(watcher, &QDBusPendingCallWatcher::finished, q, [this, watcher] {
        watcher->deleteLater();
        QDBusPendingReply<QVariant> reply = *watcher;
        if (reply.isError()) {
            return;
        }
        const auto list = reply.value().toStringList();
        qCInfo(lcSniHost) << "seedExistingItems found" << list.size() << "pre-existing tray item(s):" << list;
        for (const auto& canonical : list) {
            onItemRegistered(canonical);
        }
    });
}

void StatusNotifierHost::Private::onItemRegistered(const QString& canonical)
{
    if (itemsByCanonical.contains(canonical))
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
    itemsList.append(item);
    itemsByCanonical.insert(canonical, item);
    qCInfo(lcSniHost) << "item registered:" << canonical << "→ service" << service << "path" << path
                      << "(total items now:" << itemsList.size() << ")";
    Q_EMIT q->itemAdded(item);
    Q_EMIT q->itemCountChanged();
}

void StatusNotifierHost::Private::onItemUnregistered(const QString& canonical)
{
    auto* item = itemsByCanonical.take(canonical);
    if (!item)
        return;
    // EMIT BEFORE removing from itemsList. The model's onItemRemoved
    // resolves the row via host.items().indexOf(item); if we removed
    // first, that lookup would return -1 and beginRemoveRows() would
    // never be called — leaving the QML Repeater holding a delegate
    // bound to a deleteLater()'d item. Order: signal first (model
    // reads the still-valid index), then remove from the storage
    // containers, then defer the QObject delete.
    Q_EMIT q->itemRemoved(item);
    itemsList.removeOne(item);
    Q_EMIT q->itemCountChanged();
    item->deleteLater();
}

StatusNotifierHost::StatusNotifierHost(QObject* parent)
    : QObject(parent)
    , d(std::make_unique<Private>(this))
{
    registerDBusTypes();
    d->connectToWatcher();
    d->registerHost();

    // Re-register if the Watcher comes back later (e.g., it crashed
    // and respawned, or we started before any host).
    d->nameWatcher = new QDBusServiceWatcher(kWatcherService(), QDBusConnection::sessionBus(),
                                             QDBusServiceWatcher::WatchForRegistration, this);
    connect(d->nameWatcher, &QDBusServiceWatcher::serviceRegistered, this, [this](const QString&) {
        d->registerHost();
    });
}

StatusNotifierHost::~StatusNotifierHost() = default;

QList<StatusNotifierItem*> StatusNotifierHost::items() const
{
    return d->itemsList;
}

int StatusNotifierHost::itemCount() const
{
    return d->itemsList.size();
}

StatusNotifierItem* StatusNotifierHost::itemAt(int index) const
{
    if (index < 0 || index >= d->itemsList.size())
        return nullptr;
    return d->itemsList.value(index);
}

void StatusNotifierHost::_q_remoteItemRegistered(const QString& canonical)
{
    d->onItemRegistered(canonical);
}

void StatusNotifierHost::_q_remoteItemUnregistered(const QString& canonical)
{
    d->onItemUnregistered(canonical);
}

} // namespace PhosphorServiceSni
