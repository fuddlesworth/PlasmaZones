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
#include <QSet>
#include <QStringList>

Q_LOGGING_CATEGORY(lcSniHost, "phosphor.service.sni.host")

namespace PhosphorServiceSni {

namespace {
// Inline helpers: each call returns a QStringLiteral-backed QString.
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
    /// Set true when our passive watcher gets promoted to canonical
    /// owner. The next `seedExistingItems` reply will see an empty
    /// authoritative list (items registered with the prior owner, not
    /// with us), so the zombie reconciliation must be SKIPPED for that
    /// one cycle: tray apps generally do not re-register on ownership
    /// change, and reaping their canonicals would empty the tray. The
    /// flag is cleared on the seed call so subsequent same-watcher
    /// recovery cycles still get reconciled.
    bool skipNextZombieReap = false;

    // Items in registration order. The model maps row → item by
    // index, so the storage MUST be ordered: earlier rev used a
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
                      << "(if false, another shell (likely plasma) is the canonical watcher)";

    auto bus = QDBusConnection::sessionBus();

    // Wire item-registered / item-unregistered. If we own the watcher
    // service, prefer the in-process Qt signal (one direct call, no
    // bus round-trip); the bus loopback would fire too and
    // onItemRegistered's contains() guard would suppress it but at
    // the cost of a wasted dispatch per item. When another process
    // owns the watcher we have to rely on the bus subscription.
    if (watcher->isServiceOwner()) {
        QObject::connect(watcher, &StatusNotifierWatcher::StatusNotifierItemRegistered, q, [this](const QString& c) {
            onItemRegistered(c);
        });
        QObject::connect(watcher, &StatusNotifierWatcher::StatusNotifierItemUnregistered, q, [this](const QString& c) {
            onItemUnregistered(c);
        });
    } else {
        bus.connect(kWatcherService(), kWatcherPath(), kWatcherInterface(),
                    QStringLiteral("StatusNotifierItemRegistered"), q, SLOT(_q_remoteItemRegistered(QString)));
        bus.connect(kWatcherService(), kWatcherPath(), kWatcherInterface(),
                    QStringLiteral("StatusNotifierItemUnregistered"), q, SLOT(_q_remoteItemUnregistered(QString)));
        // If our passive watcher is later promoted to canonical owner
        // (the prior owner exited and `tryClaimOwnership` succeeded),
        // tear down the bus subscriptions and switch to local-signal
        // wiring. Without this, every registration would dispatch via
        // both the bus loopback AND the local signal; the contains()
        // guard makes the duplicate idempotent but each item still
        // pays an extra DBus round-trip and a duplicate log line.
        QObject::connect(watcher, &StatusNotifierWatcher::promotedToOwner, q, [this]() {
            auto bus = QDBusConnection::sessionBus();
            bus.disconnect(kWatcherService(), kWatcherPath(), kWatcherInterface(),
                           QStringLiteral("StatusNotifierItemRegistered"), q, SLOT(_q_remoteItemRegistered(QString)));
            bus.disconnect(kWatcherService(), kWatcherPath(), kWatcherInterface(),
                           QStringLiteral("StatusNotifierItemUnregistered"), q,
                           SLOT(_q_remoteItemUnregistered(QString)));
            // Qt::UniqueConnection on both wires for parity with the
            // watcher-side rewire; defensive even though
            // onOwnershipReleased ensures one-shot promotion today.
            QObject::connect(
                watcher, &StatusNotifierWatcher::StatusNotifierItemRegistered, q,
                [this](const QString& c) {
                    onItemRegistered(c);
                },
                Qt::UniqueConnection);
            QObject::connect(
                watcher, &StatusNotifierWatcher::StatusNotifierItemUnregistered, q,
                [this](const QString& c) {
                    onItemUnregistered(c);
                },
                Qt::UniqueConnection);
            // The next seedExistingItems will see an empty authoritative
            // list (items registered with the prior owner, not with us).
            // Skip the zombie reaper for that one cycle so tray apps that
            // do not re-register on ownership change keep their slots.
            skipNextZombieReap = true;
        });
    }
}

void StatusNotifierHost::Private::registerHost()
{
    auto bus = QDBusConnection::sessionBus();
    // applicationPid() is process-stable, so the host name is too;
    // recompute once and reuse. Subsequent calls (after the watcher
    // respawned) only need to re-issue the RegisterStatusNotifierHost
    // notification + the item seed, not the local name claim.
    bool isFirstRegistration = false;
    if (hostServiceName.isEmpty()) {
        hostServiceName = QStringLiteral("org.kde.StatusNotifierHost-%1").arg(QCoreApplication::applicationPid());
        const auto reply =
            bus.interface()->registerService(hostServiceName, QDBusConnectionInterface::DontQueueService);
        if (!reply.isValid() || reply.value() != QDBusConnectionInterface::ServiceRegistered) {
            qCWarning(lcSniHost) << "failed to register host service" << hostServiceName << ":"
                                 << (reply.isValid() ? QStringLiteral("not registered") : reply.error().message());
            hostServiceName.clear();
            return;
        }
        isFirstRegistration = true;
    }

    // Tell whichever process owns the Watcher service that we're a
    // host. Async; if the Watcher isn't up yet, the NameOwnerChanged
    // wire (below) will retry once it appears.
    QDBusInterface watcherIface(kWatcherService(), kWatcherPath(), kWatcherInterface(), bus);
    if (isFirstRegistration) {
        qCInfo(lcSniHost) << "host name registered:" << hostServiceName << "watcher iface valid?"
                          << watcherIface.isValid();
    }
    if (watcherIface.isValid()) {
        watcherIface.asyncCall(QStringLiteral("RegisterStatusNotifierHost"), hostServiceName);
        seedExistingItems();
    } else if (!isFirstRegistration) {
        // Deferred-retry path: the watcher disappeared between our
        // initial registration and this re-registration call. The
        // NameOwnerChanged wire will fire registerHost() again when
        // the watcher reappears; log so the gap is observable.
        qCInfo(lcSniHost) << "watcher iface not available, deferring registration for" << hostServiceName;
    }
}

void StatusNotifierHost::Private::seedExistingItems()
{
    // Read the property: items that registered before we started
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
        // Reconcile: reap zombies whose owners died while the prior
        // watcher was down. The new watcher's authoritative set is the
        // canonicals it just published; anything we still hold that's
        // not in that set has lost its NameOwnerChanged signal source.
        //
        // EXCEPT after a passive→active promotion: the items we
        // accumulated via bus-subscription belonged to the prior owner
        // (Plasma), the freshly-queried list dispatches to our own
        // watcher whose m_items is empty, and tray apps do not
        // re-register on ownership change. Reaping under that
        // condition would empty the tray. The promotedToOwner handler
        // sets skipNextZombieReap; honour it once.
        if (!skipNextZombieReap) {
            const QSet<QString> incoming(list.cbegin(), list.cend());
            QStringList zombies;
            for (auto it = itemsByCanonical.cbegin(); it != itemsByCanonical.cend(); ++it) {
                if (!incoming.contains(it.key()))
                    zombies.append(it.key());
            }
            for (const auto& canonical : zombies) {
                qCInfo(lcSniHost) << "reaping zombie item after watcher respawn:" << canonical;
                onItemUnregistered(canonical);
            }
        } else {
            qCInfo(lcSniHost) << "skipping zombie reconciliation: this is a passive→active promotion seed";
            skipNextZombieReap = false;
        }
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
    // EMIT BEFORE removing from itemsList. The current
    // StatusNotifierItemModel maintains its own mirror and looks up
    // the row from its private state, so it does not depend on this
    // ordering, but external listeners (custom dashboards, test
    // harnesses) that walk `host->items()` from within an
    // `itemRemoved` slot expect the item to still appear in the list
    // for the duration of the slot. Order: signal first (observers
    // read the still-listed item), then remove from the storage
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
