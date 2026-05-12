// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <QDBusContext>
#include <QDBusServiceWatcher>
#include <QHash>
#include <QObject>
#include <QStringList>

namespace PhosphorServices {

/// The org.kde.StatusNotifierWatcher session-bus service. Items
/// register themselves via RegisterStatusNotifierItem; hosts (shells)
/// announce themselves via RegisterStatusNotifierHost. The Watcher
/// rebroadcasts changes via signals + the RegisteredStatusNotifierItems
/// property so multiple shell-hosts can stay in sync.
///
/// The class is private to the library — public API is StatusNotifierHost.
/// We expose the service iff no other process is already owning the
/// well-known name; if another shell is already on the bus, our host
/// just registers with theirs and our Watcher instance idles.
class StatusNotifierWatcher : public QObject, protected QDBusContext
{
    Q_OBJECT
    // The Watcher properties — exported via DBus adaptor.
    Q_PROPERTY(QStringList RegisteredStatusNotifierItems READ registeredItems NOTIFY registeredItemsChanged)
    Q_PROPERTY(bool IsStatusNotifierHostRegistered READ isHostRegistered NOTIFY hostRegisteredChanged)
    Q_PROPERTY(int ProtocolVersion READ protocolVersion CONSTANT)

public:
    explicit StatusNotifierWatcher(QObject* parent = nullptr);
    ~StatusNotifierWatcher() override;

    /// True when we successfully claimed the well-known service name.
    /// If false, another process owns the Watcher and we should defer
    /// to it (our host should still register, but with the other one).
    [[nodiscard]] bool isServiceOwner() const;

    [[nodiscard]] QStringList registeredItems() const;
    [[nodiscard]] bool isHostRegistered() const;
    [[nodiscard]] int protocolVersion() const
    {
        return 0;
    }

public Q_SLOTS:
    // DBus-callable methods. The Adaptor forwards here.
    void RegisterStatusNotifierItem(const QString& service);
    void RegisterStatusNotifierHost(const QString& service);

Q_SIGNALS:
    // DBus signals — Adaptor proxies these onto the bus.
    void StatusNotifierItemRegistered(const QString& service);
    void StatusNotifierItemUnregistered(const QString& service);
    void StatusNotifierHostRegistered();
    void StatusNotifierHostUnregistered();

    // Local-only — Watcher → Host wiring; the Adaptor ignores these.
    void registeredItemsChanged();
    void hostRegisteredChanged();

private Q_SLOTS:
    void onServiceUnregistered(const QString& service);

private:
    [[nodiscard]] QString canonicalItemService(const QString& serviceOrPath, const QString& senderUniqueName) const;

    QDBusServiceWatcher* m_busWatcher;
    bool m_serviceOwner = false;

    // Map: canonical "uniqueName/path" → owning bus unique name. The
    // unique name is what NameOwnerChanged signals on, so we key by it
    // to know when an item's process has died.
    struct ItemEntry
    {
        QString uniqueName; ///< ":1.42"
        QString canonical; ///< ":1.42/StatusNotifierItem"
    };
    QHash<QString, ItemEntry> m_items; ///< keyed by canonical
    QHash<QString, QStringList> m_byOwner; ///< unique name → canonicals it owns

    QHash<QString, QString> m_hosts; ///< canonical "host-pid" → unique name
};

} // namespace PhosphorServices
