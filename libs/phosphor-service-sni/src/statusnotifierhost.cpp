// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorServiceSni/StatusNotifierHost.h>

#include <PhosphorServiceSni/StatusNotifierItem.h>

#include "dbustypes.h"
#include "statusnotifierwatcher.h"

#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusServiceWatcher>
#include <QDebug>
#include <QHash>
#include <QLoggingCategory>
#include <QSet>
#include <QStringList>

namespace {
Q_LOGGING_CATEGORY(lcSniHost, "phosphor.service.sni.host")
} // namespace

namespace PhosphorServiceSni {

namespace {
// Inline helpers: each call returns a QStringLiteral-backed QString.
// CLAUDE.md forbids raw "..." with QString APIs; inline-function form
// keeps the call sites clean while honouring the rule.
inline QString watcherService()
{
    return QStringLiteral("org.kde.StatusNotifierWatcher");
}
inline QString watcherPath()
{
    return QStringLiteral("/StatusNotifierWatcher");
}
inline QString watcherInterface()
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
    /// @p skipZombieReap is true only for the seed issued by a passive→active
    /// promotion: that seed's authoritative list is empty (items registered
    /// with the prior owner, not with us) and tray apps do not re-register on
    /// ownership change, so reaping would empty the tray. Passing it per call
    /// instead of latching a member flag ties the skip to exactly the one seed
    /// it belongs to: a latch either survived into a later legitimate recovery
    /// cycle (when no seed followed the promotion) or was consumed early by a
    /// pre-promotion seed still in flight.
    void seedExistingItems(bool skipZombieReap);
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
        // A failed subscription here means the host never learns about a
        // single tray item, and the only symptom would be an empty tray with
        // a clean log. Both returns are checked so that failure mode is
        // visible at default log levels.
        if (!bus.connect(watcherService(), watcherPath(), watcherInterface(),
                         QStringLiteral("StatusNotifierItemRegistered"), q, SLOT(_q_remoteItemRegistered(QString)))) {
            qCWarning(lcSniHost) << "failed to subscribe to StatusNotifierItemRegistered; tray will not populate";
        }
        if (!bus.connect(watcherService(), watcherPath(), watcherInterface(),
                         QStringLiteral("StatusNotifierItemUnregistered"), q,
                         SLOT(_q_remoteItemUnregistered(QString)))) {
            qCWarning(lcSniHost) << "failed to subscribe to StatusNotifierItemUnregistered; stale items will linger";
        }
        // If our passive watcher is later promoted to canonical owner
        // (the prior owner exited and `tryClaimOwnership` succeeded),
        // tear down the bus subscriptions and switch to local-signal
        // wiring. Without this, every registration would dispatch via
        // both the bus loopback AND the local signal; the contains()
        // guard makes the duplicate idempotent but each item still
        // pays an extra DBus round-trip and a duplicate log line.
        QObject::connect(watcher, &StatusNotifierWatcher::promotedToOwner, q, [this]() {
            auto bus = QDBusConnection::sessionBus();
            bus.disconnect(watcherService(), watcherPath(), watcherInterface(),
                           QStringLiteral("StatusNotifierItemRegistered"), q, SLOT(_q_remoteItemRegistered(QString)));
            bus.disconnect(watcherService(), watcherPath(), watcherInterface(),
                           QStringLiteral("StatusNotifierItemUnregistered"), q,
                           SLOT(_q_remoteItemUnregistered(QString)));
            // NO Qt::UniqueConnection here, unlike the watcher-side
            // rewire it used to mirror. That flag is only supported when
            // the slot is a pointer to a member function; passing it with
            // a functor trips an assert inside QObject::connect and aborts
            // the process. The watcher-side wire can carry it precisely
            // because it connects to &StatusNotifierWatcher::onServiceUnregistered.
            //
            // Nothing is lost: promotion is one-shot. onOwnershipReleased
            // returns early once m_serviceOwner is set, and tryClaimOwnership
            // sets it before promotedToOwner is emitted, so this lambda runs
            // at most once per watcher and there is no duplicate to suppress.
            QObject::connect(watcher, &StatusNotifierWatcher::StatusNotifierItemRegistered, q,
                             [this](const QString& c) {
                                 onItemRegistered(c);
                             });
            QObject::connect(watcher, &StatusNotifierWatcher::StatusNotifierItemUnregistered, q,
                             [this](const QString& c) {
                                 onItemUnregistered(c);
                             });
            // Issue the promotion's own seed HERE, tagged to skip the zombie
            // reaper: its authoritative list is empty (items registered with
            // the prior owner, not with us) and tray apps do not re-register
            // on ownership change. Issuing seed and skip as one step ties the
            // skip to exactly this seed instead of arming a latch for
            // whichever seed happens to run next.
            seedExistingItems(true);
        });
    }
}

void StatusNotifierHost::Private::registerHost()
{
    auto bus = QDBusConnection::sessionBus();
    // interface() is null on an unconnected bus (no DBUS_SESSION_BUS_ADDRESS:
    // headless CI, stripped container). Every deref below goes through this
    // local, so a bus-less start degrades with a warning instead of a crash.
    QDBusConnectionInterface* iface = bus.interface();
    if (!iface) {
        qCWarning(lcSniHost) << "no session bus connection; SNI host disabled";
        return;
    }
    // applicationPid() is process-stable, so the host name is too;
    // recompute once and reuse. Subsequent calls (after the watcher
    // respawned) only need to re-issue the RegisterStatusNotifierHost
    // notification + the item seed, not the local name claim.
    bool isFirstRegistration = false;
    if (hostServiceName.isEmpty()) {
        hostServiceName = QStringLiteral("org.kde.StatusNotifierHost-%1").arg(QCoreApplication::applicationPid());
        const auto reply = iface->registerService(hostServiceName, QDBusConnectionInterface::DontQueueService);
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
    //
    // Presence is checked against the BUS DAEMON, not by constructing a
    // QDBusInterface. `QDBusInterface`'s constructor performs a BLOCKING
    // introspection call to the target service, so when the watcher is absent it
    // stalls this thread for the full D-Bus reply timeout (25 s) before
    // `isValid()` can report false — on the GUI thread of whatever hosts the
    // tray. That is reachable on a real desktop every time the watcher is
    // briefly gone (a Plasma restart), not just under test. `isServiceRegistered`
    // asks the bus daemon, which is always present and answers immediately, and
    // the call below is built as a plain message so nothing introspects at all.
    const bool watcherPresent = iface->isServiceRegistered(watcherService()).value();
    if (isFirstRegistration) {
        qCInfo(lcSniHost) << "host name registered:" << hostServiceName << "watcher present?" << watcherPresent;
    }
    if (watcherPresent) {
        QDBusMessage registerCall = QDBusMessage::createMethodCall(watcherService(), watcherPath(), watcherInterface(),
                                                                   QStringLiteral("RegisterStatusNotifierHost"));
        registerCall << hostServiceName;
        // Surface a rejected host registration: silently dropping the reply
        // would leave IsStatusNotifierHostRegistered false with a clean log.
        auto* registerWatcher = new QDBusPendingCallWatcher(bus.asyncCall(registerCall), q);
        QObject::connect(registerWatcher, &QDBusPendingCallWatcher::finished, q, [](QDBusPendingCallWatcher* w) {
            w->deleteLater();
            const QDBusPendingReply<> reply = *w;
            if (reply.isError()) {
                qCWarning(lcSniHost) << "RegisterStatusNotifierHost rejected:" << reply.error().message();
            }
        });
        seedExistingItems(false);
    } else if (!isFirstRegistration) {
        // Deferred-retry path: the watcher disappeared between our
        // initial registration and this re-registration call. The
        // NameOwnerChanged wire will fire registerHost() again when
        // the watcher reappears; log so the gap is observable.
        qCInfo(lcSniHost) << "watcher iface not available, deferring registration for" << hostServiceName;
    }
}

void StatusNotifierHost::Private::seedExistingItems(bool skipZombieReap)
{
    // Read the property: items that registered before we started
    // need to be backfilled. Async to keep the constructor cheap.
    auto bus = QDBusConnection::sessionBus();
    QDBusMessage msg = QDBusMessage::createMethodCall(
        watcherService(), watcherPath(), QStringLiteral("org.freedesktop.DBus.Properties"), QStringLiteral("Get"));
    msg << watcherInterface() << QStringLiteral("RegisteredStatusNotifierItems");
    auto pending = bus.asyncCall(msg);
    // Parenting the watcher to `q` is what makes the raw `this` (Private*)
    // capture below safe: Private is owned by q (it is q's pimpl), so if q is
    // destroyed the watcher is destroyed in the same teardown, which severs
    // this connection before the captured `this` can dangle. The connection is
    // also bound to `q` as the context object, so a queued `finished` delivery
    // is dropped once q is gone. Do not reparent the watcher off q without
    // switching the capture to a QPointer.
    auto* watcher = new QDBusPendingCallWatcher(pending, q);
    QObject::connect(watcher, &QDBusPendingCallWatcher::finished, q, [this, watcher, skipZombieReap] {
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
        // condition would empty the tray. The promotion handler issues
        // its seed with skipZombieReap set; only that seed skips.
        if (!skipZombieReap) {
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
    // System boundary: the canonical arrives from an untrusted D-Bus peer
    // (any session process can call RegisterStatusNotifierItem). An empty
    // service (canonical beginning with '/') would construct an item that
    // issues calls against nothing; refuse loudly. The `path.startsWith('/')`
    // term is belt-and-braces only: `path` is `canonical.mid(slash)` where
    // `slash` is the first '/', so it already begins with '/' by construction
    // (the `slash < 0` return above guarantees one exists). Kept so a future
    // change to the derivation cannot silently admit a slashless path.
    if (service.isEmpty() || !path.startsWith(QLatin1Char('/'))) {
        qCWarning(lcSniHost) << "refusing malformed item canonical from bus:" << canonical;
        return;
    }

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

    // Install the re-registration watcher BEFORE the first registerHost(): if
    // the Watcher service registered in the window between registerHost()'s
    // isServiceRegistered probe and this connect, the serviceRegistered signal
    // would be missed and (with no periodic retry) the host would stay
    // unregistered for the process lifetime. Wiring it first closes that race —
    // a Watcher that appears during registerHost() now fires the retry.
    d->nameWatcher = new QDBusServiceWatcher(watcherService(), QDBusConnection::sessionBus(),
                                             QDBusServiceWatcher::WatchForRegistration, this);
    connect(d->nameWatcher, &QDBusServiceWatcher::serviceRegistered, this, [this](const QString&) {
        d->registerHost();
    });

    d->registerHost();
}

StatusNotifierHost::~StatusNotifierHost()
{
    // Release the claimed host bus name so a watcher tracking hosts via
    // NameOwnerChanged stops reporting IsStatusNotifierHostRegistered for a
    // host that no longer exists. The sibling watcher does the symmetric
    // teardown for its own name; without this, in-process construct/destroy
    // cycles (tests, a shell rebuilding its tray) leave the name owned for
    // the process lifetime.
    if (!d->hostServiceName.isEmpty()) {
        auto bus = QDBusConnection::sessionBus();
        if (QDBusConnectionInterface* iface = bus.interface()) {
            iface->unregisterService(d->hostServiceName);
        }
    }
}

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
