// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include "statusnotifierwatcher.h"

#include "statusnotifierwatcheradaptor.h"

#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDebug>

namespace PhosphorServices {

namespace {
constexpr auto kWatcherServiceName = "org.kde.StatusNotifierWatcher";
constexpr auto kWatcherObjectPath = "/StatusNotifierWatcher";
} // namespace

StatusNotifierWatcher::StatusNotifierWatcher(QObject* parent)
    : QObject(parent)
    , m_busWatcher(new QDBusServiceWatcher(this))
{
    auto bus = QDBusConnection::sessionBus();
    if (!bus.isConnected()) {
        qWarning() << "StatusNotifierWatcher: session bus not connected";
        return;
    }

    // The adaptor exposes our slots/signals/properties on DBus. It
    // takes a non-owning ref to us; QtDBus's adaptor pattern requires
    // the adaptor be a child of the object it adapts.
    new StatusNotifierWatcherAdaptor(this);

    // Try to register the object path first — if this fails we have
    // nothing to expose, regardless of name ownership.
    if (!bus.registerObject(QString::fromLatin1(kWatcherObjectPath), this)) {
        qWarning() << "StatusNotifierWatcher: registerObject failed";
        return;
    }

    // Try to claim the well-known name. If someone else already owns
    // it, fall back to passive mode: the host will register with the
    // existing watcher and ignore us. Don't queue or replace — Plasma
    // and other shells already use NoQueue here too.
    const auto reply = bus.interface()->registerService(QString::fromLatin1(kWatcherServiceName),
                                                        QDBusConnectionInterface::DontQueueService,
                                                        QDBusConnectionInterface::DontAllowReplacement);
    m_serviceOwner = reply.isValid() && reply.value() == QDBusConnectionInterface::ServiceRegistered;

    if (!m_serviceOwner) {
        // Don't keep the object registered if we aren't the canonical
        // watcher — it would advertise an empty item list and confuse
        // any apps that introspect our process by accident.
        bus.unregisterObject(QString::fromLatin1(kWatcherObjectPath));
        return;
    }

    // Watch the bus for name-owner changes so we can clean up items
    // whose processes have died.
    m_busWatcher->setConnection(bus);
    m_busWatcher->setWatchMode(QDBusServiceWatcher::WatchForUnregistration);
    connect(m_busWatcher, &QDBusServiceWatcher::serviceUnregistered, this,
            &StatusNotifierWatcher::onServiceUnregistered);
}

StatusNotifierWatcher::~StatusNotifierWatcher()
{
    if (m_serviceOwner) {
        auto bus = QDBusConnection::sessionBus();
        bus.unregisterObject(QString::fromLatin1(kWatcherObjectPath));
        bus.interface()->unregisterService(QString::fromLatin1(kWatcherServiceName));
    }
}

bool StatusNotifierWatcher::isServiceOwner() const
{
    return m_serviceOwner;
}

QStringList StatusNotifierWatcher::registeredItems() const
{
    QStringList list;
    list.reserve(m_items.size());
    for (const auto& entry : m_items) {
        list.append(entry.canonical);
    }
    return list;
}

bool StatusNotifierWatcher::isHostRegistered() const
{
    return !m_hosts.isEmpty();
}

QString StatusNotifierWatcher::canonicalItemService(const QString& serviceOrPath, const QString& senderUniqueName) const
{
    // Items register two forms in the wild:
    //
    //   (a) Plain bus name (":1.42" or "org.kde.foo") — path is
    //       implicitly "/StatusNotifierItem". This is the older KDE
    //       convention and what most C++ Qt apps emit.
    //   (b) Full object path ("/org/ayatana/NotificationItem/foo") —
    //       sender is the implicit service. GTK app indicators do
    //       this.
    //
    // Canonical form for OUR storage + signals is "uniqueName/path"
    // (so item proxies can dial it directly), regardless of which
    // form the registrar used. The unique name is always the bus
    // sender — we don't trust the well-known name the caller may have
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
    // Adaptors don't pass the sender through automatically — but
    // QDBusContext does. The adaptor base class inherits QDBusContext
    // and forwards message().service() as the unique sender name.
    // QDBus annotates it on the QDBusMessage backing this call.
    const auto sender = message().service();
    const auto canonical = canonicalItemService(service, sender);

    if (m_items.contains(canonical)) {
        return; // already registered
    }

    ItemEntry entry{sender, canonical};
    m_items.insert(canonical, entry);
    m_byOwner[sender].append(canonical);
    if (!m_busWatcher->watchedServices().contains(sender)) {
        m_busWatcher->addWatchedService(sender);
    }

    Q_EMIT StatusNotifierItemRegistered(canonical);
    Q_EMIT registeredItemsChanged();
}

void StatusNotifierWatcher::RegisterStatusNotifierHost(const QString& service)
{
    if (!m_serviceOwner) {
        return;
    }
    const auto sender = message().service();
    if (m_hosts.contains(service)) {
        return;
    }
    m_hosts.insert(service, sender);
    if (!m_busWatcher->watchedServices().contains(sender)) {
        m_busWatcher->addWatchedService(sender);
    }
    const bool wasRegistered = m_hosts.size() > 1;
    if (!wasRegistered) {
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

    bool hostsChanged = false;
    for (auto it = m_hosts.begin(); it != m_hosts.end();) {
        if (it.value() == service) {
            it = m_hosts.erase(it);
            hostsChanged = true;
        } else {
            ++it;
        }
    }

    m_busWatcher->removeWatchedService(service);

    if (itemsChanged) {
        Q_EMIT registeredItemsChanged();
    }
    if (hostsChanged) {
        Q_EMIT StatusNotifierHostUnregistered();
        if (m_hosts.isEmpty()) {
            Q_EMIT hostRegisteredChanged();
        }
    }
}

} // namespace PhosphorServices
