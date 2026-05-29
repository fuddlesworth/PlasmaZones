// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include "statusnotifierwatcher.h"

#include "statusnotifierwatcheradaptor.h"

#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusObjectPath>
#include <QLoggingCategory>

#include <algorithm>

Q_LOGGING_CATEGORY(lcSniWatcher, "phosphor.service.sni.watcher")

namespace PhosphorServiceSni {

namespace {
// Spec-defined DBus identifiers. Declared as inline functions
// returning QString rather than constexpr `const char*` so each
// callsite gets a QStringLiteral allocation (project rule: never
// raw `"..."` with QString APIs) without paying for QString::fromLatin1
// conversions at every use.
inline QString kWatcherServiceName()
{
    return QStringLiteral("org.kde.StatusNotifierWatcher");
}
inline QString kWatcherObjectPath()
{
    return QStringLiteral("/StatusNotifierWatcher");
}
} // namespace

StatusNotifierWatcher::StatusNotifierWatcher(QObject* parent)
    : QObject(parent)
    , m_busWatcher(new QDBusServiceWatcher(this))
{
    auto bus = QDBusConnection::sessionBus();
    if (!bus.isConnected()) {
        qCWarning(lcSniWatcher) << "session bus not connected";
        return;
    }

    // The adaptor exposes our slots/signals/properties on DBus. It
    // takes a non-owning ref to us; QtDBus's adaptor pattern requires
    // the adaptor be a child of the object it adapts.
    new StatusNotifierWatcherAdaptor(this);

    // Watch for the canonical watcher name being released. If we boot
    // as a passive watcher (Plasma owned it) and Plasma later exits,
    // this fires so we can promote ourselves rather than leaving the
    // tray broken with no canonical watcher on the bus.
    m_ownershipWatcher =
        new QDBusServiceWatcher(kWatcherServiceName(), bus, QDBusServiceWatcher::WatchForUnregistration, this);
    connect(m_ownershipWatcher, &QDBusServiceWatcher::serviceUnregistered, this,
            &StatusNotifierWatcher::onOwnershipReleased);

    tryClaimOwnership();

    if (!m_serviceOwner) {
        // Don't keep an object registered we don't own; passive mode
        // just waits for the prior owner to release via
        // onOwnershipReleased.
        return;
    }

    // Watch the bus for name-owner changes so we can clean up items
    // whose processes have died.
    m_busWatcher->setConnection(bus);
    m_busWatcher->setWatchMode(QDBusServiceWatcher::WatchForUnregistration);
    connect(m_busWatcher, &QDBusServiceWatcher::serviceUnregistered, this,
            &StatusNotifierWatcher::onServiceUnregistered);
}

bool StatusNotifierWatcher::tryClaimOwnership()
{
    auto bus = QDBusConnection::sessionBus();
    if (!bus.isConnected()) {
        return false;
    }

    // Try to register the object path first. If this fails we have
    // nothing to expose, regardless of name ownership.
    if (!bus.registerObject(kWatcherObjectPath(), this)) {
        qCWarning(lcSniWatcher) << "registerObject failed";
        return false;
    }

    // Try to claim the well-known name. If someone else already owns
    // it, fall back to passive mode: the host will register with the
    // existing watcher and ignore us. Don't queue or replace: Plasma
    // and other shells already use NoQueue here too.
    const auto reply =
        bus.interface()->registerService(kWatcherServiceName(), QDBusConnectionInterface::DontQueueService,
                                         QDBusConnectionInterface::DontAllowReplacement);
    m_serviceOwner = reply.isValid() && reply.value() == QDBusConnectionInterface::ServiceRegistered;

    if (!m_serviceOwner) {
        // Drop the object registration so a future introspection
        // against our process bus name doesn't see a stub watcher.
        bus.unregisterObject(kWatcherObjectPath());
    }
    return m_serviceOwner;
}

void StatusNotifierWatcher::onOwnershipReleased(const QString& service)
{
    if (service != kWatcherServiceName() || m_serviceOwner) {
        return;
    }
    if (!tryClaimOwnership()) {
        return;
    }
    qCInfo(lcSniWatcher) << "promoted to canonical watcher after prior owner exited";

    // We won; wire up the unregistration-tracking watcher we skipped
    // in the passive ctor path.
    auto bus = QDBusConnection::sessionBus();
    m_busWatcher->setConnection(bus);
    m_busWatcher->setWatchMode(QDBusServiceWatcher::WatchForUnregistration);
    connect(m_busWatcher, &QDBusServiceWatcher::serviceUnregistered, this,
            &StatusNotifierWatcher::onServiceUnregistered, Qt::UniqueConnection);
    // Let StatusNotifierHost switch its registration wiring from the
    // bus-subscription path (set up in passive mode) to local-signal
    // delivery so item registrations stop double-dispatching.
    Q_EMIT promotedToOwner();
}

StatusNotifierWatcher::~StatusNotifierWatcher()
{
    if (m_serviceOwner) {
        auto bus = QDBusConnection::sessionBus();
        bus.unregisterObject(kWatcherObjectPath());
        bus.interface()->unregisterService(kWatcherServiceName());
    }
}

bool StatusNotifierWatcher::isServiceOwner() const
{
    return m_serviceOwner;
}

QStringList StatusNotifierWatcher::registeredItems() const
{
    // Stable sort by canonical name so successive reads of the
    // RegisteredStatusNotifierItems DBus property return the items
    // in a deterministic order. The QHash iteration order is bucket-
    // dependent and changes across registrations; an observer
    // diffing successive lists would see spurious "different" rows.
    // The sorted list is cached and invalidated on item register /
    // unregister so polling consumers don't pay O(N log N) per read.
    if (m_sortedDirty) {
        m_sorted.clear();
        m_sorted.reserve(m_items.size());
        for (const auto& entry : m_items) {
            m_sorted.append(entry.canonical);
        }
        std::sort(m_sorted.begin(), m_sorted.end());
        m_sortedDirty = false;
    }
    return m_sorted;
}

bool StatusNotifierWatcher::isHostRegistered() const
{
    return !m_hosts.isEmpty();
}

QString StatusNotifierWatcher::canonicalItemService(const QString& serviceOrPath, const QString& senderUniqueName) const
{
    // Items register two forms in the wild:
    //
    //   (a) Plain bus name (":1.42" or "org.kde.foo"): path is
    //       implicitly "/StatusNotifierItem". This is the older KDE
    //       convention and what most C++ Qt apps emit.
    //   (b) Full object path ("/org/ayatana/NotificationItem/foo"):
    //       sender is the implicit service. GTK app indicators do
    //       this.
    //
    // Canonical form for OUR storage + signals is "uniqueName/path"
    // (so item proxies can dial it directly), regardless of which
    // form the registrar used. The unique name is always the bus
    // sender: we don't trust the well-known name the caller may have
    // passed in.
    QString path;
    if (serviceOrPath.startsWith(QLatin1Char('/'))) {
        path = serviceOrPath;
    } else {
        path = QStringLiteral("/StatusNotifierItem");
    }
    return senderUniqueName + path;
}

void StatusNotifierWatcher::RegisterStatusNotifierItem(const QString& service)
{
    if (!m_serviceOwner) {
        return;
    }
    // Input validation at the DBus boundary. Reject empty service
    // and obviously-malformed paths (anything starting with `/` that
    // isn't a valid DBus object path). The spec requires either a
    // bus name or an absolute object path; treat anything else as a
    // misbehaving client and drop the request silently. CLAUDE.md
    // mandates "input validation at system boundaries".
    if (service.isEmpty()) {
        return;
    }
    if (service.startsWith(QLatin1Char('/'))) {
        // Object-path form: validate via QDBusObjectPath which enforces
        // the spec's `[/A-Za-z0-9_]+` grammar. A misbehaving client could
        // otherwise register `/<spaces>` or other forbidden characters,
        // producing a canonical that proxies later fail to dial and
        // pollutes m_items / m_sorted with garbage.
        if (QDBusObjectPath(service).path().isEmpty()) {
            return;
        }
    }
    // Adaptors don't pass the sender through automatically, but
    // QDBusContext does. The adaptor base class inherits QDBusContext
    // and forwards message().service() as the unique sender name;
    // QDBus annotates it on the QDBusMessage backing this call. When
    // this method is reached outside a real D-Bus dispatch (e.g. a
    // direct in-process call from a test fixture), message().service()
    // is empty and the canonical would become a bare "/Item" with no
    // owner key, breaking owner-tracked teardown. Reject that path.
    if (!calledFromDBus() || message().service().isEmpty()) {
        return;
    }
    const auto sender = message().service();
    const auto canonical = canonicalItemService(service, sender);

    if (m_items.contains(canonical)) {
        return; // already registered
    }

    ItemEntry entry{sender, canonical};
    m_items.insert(canonical, entry);
    // Compute the dedup gate BEFORE the append so "already watched" is
    // independent of the row we're about to add. QDBusServiceWatcher
    // appends to an internal list without dedup; the dbus-daemon match-
    // rule is shared so repeated adds are harmless on the wire, but the
    // watcher would accumulate stale entries we never clean up. Skip
    // the add if THIS sender is already watched via prior items or via
    // a host registration for the same sender; onServiceUnregistered's
    // single removeWatchedService then matches symmetrically.
    const bool senderAlreadyWatched = !m_byOwner.value(sender).isEmpty()
        || std::any_of(m_hosts.cbegin(), m_hosts.cend(), [&sender](const QString& v) {
               return v == sender;
           });
    m_byOwner[sender].append(canonical);
    if (!senderAlreadyWatched) {
        m_busWatcher->addWatchedService(sender);
    }
    m_sortedDirty = true;

    Q_EMIT StatusNotifierItemRegistered(canonical);
    Q_EMIT registeredItemsChanged();
}

void StatusNotifierWatcher::RegisterStatusNotifierHost(const QString& service)
{
    if (!m_serviceOwner) {
        return;
    }
    // Mirror the item-registration guard: a direct in-process call
    // (test fixture, accidental consumer that links this class) has
    // no DBus sender. We track hosts by unique bus name in m_byOwner
    // semantics; recording an empty sender would let one stray
    // in-process call permanently claim a host slot keyed by "".
    if (!calledFromDBus() || message().service().isEmpty()) {
        return;
    }
    // Spec format: `org.kde.StatusNotifierHost-<pid>`. Reject any
    // string that doesn't match the prefix; accepting arbitrary names
    // from random clients would let a misbehaving process spam the
    // host list. Empty also dropped.
    if (service.isEmpty() || !service.startsWith(QStringLiteral("org.kde.StatusNotifierHost-"))) {
        return;
    }
    const auto sender = message().service();
    if (m_hosts.contains(service)) {
        return;
    }
    // Match the item-registration symmetry: addWatchedService is gated
    // on "first tracking for this sender" so the single
    // removeWatchedService in onServiceUnregistered clears cleanly.
    // The owner is "already watched" if it has items OR another host
    // entry (a sender that already registered an item then registers a
    // host should not re-add).
    const bool senderAlreadyWatched = !m_byOwner.value(sender).isEmpty()
        || std::any_of(m_hosts.cbegin(), m_hosts.cend(), [&sender](const QString& v) {
               return v == sender;
           });
    const bool wasEmpty = m_hosts.isEmpty();
    m_hosts.insert(service, sender);
    if (!senderAlreadyWatched) {
        m_busWatcher->addWatchedService(sender);
    }
    // Fire hostRegisteredChanged only on the false → true transition
    // (i.e., first host). Subsequent host registrations keep it true
    // and don't need a re-emit per CLAUDE.md "only emit on change".
    if (wasEmpty) {
        Q_EMIT hostRegisteredChanged();
    }
    Q_EMIT StatusNotifierHostRegistered();
}

void StatusNotifierWatcher::onServiceUnregistered(const QString& service)
{
    // service is the unique name (":1.xx") of a process that died.
    // Reap every item + host it owned.
    bool itemsChanged = false;
    if (m_byOwner.contains(service)) {
        const auto canonicals = m_byOwner.take(service);
        for (const auto& canonical : canonicals) {
            if (m_items.remove(canonical) > 0) {
                Q_EMIT StatusNotifierItemUnregistered(canonical);
                itemsChanged = true;
            }
        }
    }

    int hostsRemoved = 0;
    for (auto it = m_hosts.begin(); it != m_hosts.end();) {
        if (it.value() == service) {
            it = m_hosts.erase(it);
            ++hostsRemoved;
        } else {
            ++it;
        }
    }

    // Only remove the bus watch when this owner actually had something
    // we were tracking; the watcher is idempotent but the call still
    // costs a hash lookup + DBus match-rule rebuild on a heavily
    // loaded session bus.
    if (itemsChanged || hostsRemoved > 0) {
        m_busWatcher->removeWatchedService(service);
    }

    if (itemsChanged) {
        m_sortedDirty = true;
        Q_EMIT registeredItemsChanged();
    }
    if (hostsRemoved > 0) {
        // Spec says one StatusNotifierHostUnregistered per host that
        // went away; if a single owner had multiple host names
        // registered, emit one signal per host so observers can keep
        // accurate counts.
        for (int i = 0; i < hostsRemoved; ++i) {
            Q_EMIT StatusNotifierHostUnregistered();
        }
        if (m_hosts.isEmpty()) {
            Q_EMIT hostRegisteredChanged();
        }
    }
}

} // namespace PhosphorServiceSni
