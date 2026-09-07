// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QAbstractListModel>
#include <QDateTime>
#include <QList>
#include <QObject>
#include <QString>

#include <memory>

namespace PhosphorServiceNotifications {
class Notification;
class NotificationModel;
class NotificationServer;
} // namespace PhosphorServiceNotifications

namespace PhosphorShellApp {

// The shell's notification centre: the org.freedesktop.Notifications
// server plus the RETAINED list the notification panel shows, installed as
// the `NotificationRegistry` context property by src/shell/main.cpp.
//
// Why a second list at all. NotificationModel is a view of what is
// currently LIVE: a notification that expires is closed by the server and
// its row disappears. That is exactly right for the toast stack, and
// exactly wrong for a centre, which exists to show what you missed while
// you were looking elsewhere. This class therefore snapshots each
// notification as it arrives and keeps it until the user clears it.
//
// A snapshot, not a pointer. The server owns every Notification and
// deletes it on close, so a list of pointers would dangle the moment a
// toast expired — which is precisely the case a centre is for. The fields
// are copied out at arrival instead, and `id` is kept so an action on a
// still-live entry can be routed back to the server.
//
// One server per process. The well-known bus name admits exactly one
// owner, so a session already running dunst / mako / Plasma's daemon
// leaves `serverActive` false and this list permanently empty. That is a
// hard conflict rather than a degraded mode, and the panel says so instead
// of showing an empty list with no explanation.
class NotificationController : public QAbstractListModel
{
    Q_OBJECT
    /// Whether this process actually owns org.freedesktop.Notifications.
    /// False when another notification daemon holds the name.
    Q_PROPERTY(bool serverActive READ isServerActive NOTIFY serverActiveChanged)
    /// Retained entries, newest first.
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)
    /// Entries that arrived since the panel was last opened. Drives the bar
    /// button's badge, which is why it is separate from `count`: the badge
    /// answers "is there anything new", not "how much is in the list".
    Q_PROPERTY(int unreadCount READ unreadCount NOTIFY unreadCountChanged)

public:
    enum Roles {
        IdRole = Qt::UserRole + 1,
        AppNameRole,
        AppIconRole,
        SummaryRole,
        BodyRole,
        TimestampRole,
        UrgencyRole,
        // Whether the server still holds this notification. A live entry
        // can be dismissed or have an action invoked; a retained one is
        // only a record, and the panel dims its actions accordingly.
        LiveRole,
    };
    Q_ENUM(Roles)

    /// Owns the server it constructs, which is what claims the bus name.
    explicit NotificationController(QObject* parent = nullptr);
    /// Dependency-injected: drives an EXISTING server and does not own it.
    /// Tests use this with a server on a private peer-to-peer bus, so the
    /// whole retention path runs without a session daemon and without the
    /// name conflict a second real server would hit. Passing null falls
    /// back to constructing one, so a caller cannot accidentally build a
    /// controller with no server behind it.
    NotificationController(PhosphorServiceNotifications::NotificationServer* server, QObject* parent = nullptr);
    ~NotificationController() override;

    [[nodiscard]] bool isServerActive() const;
    [[nodiscard]] int unreadCount() const;

    [[nodiscard]] int rowCount(const QModelIndex& parent = {}) const override;
    [[nodiscard]] QVariant data(const QModelIndex& index, int role) const override;
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;

    /// The live model, for a surface that wants only what is on screen now
    /// (the toast stack). Never null.
    [[nodiscard]] PhosphorServiceNotifications::NotificationModel* liveModel() const;

    /// Drop one retained entry. Closes it server-side first when it is
    /// still live, so dismissing from the centre also takes the toast down
    /// rather than leaving the two views disagreeing.
    Q_INVOKABLE void dismiss(uint id);
    /// Drop every retained entry, closing the ones still live.
    Q_INVOKABLE void clear();
    /// Invoke a notification's action by key. A no-op for an entry the
    /// server no longer holds: the sending application has been told the
    /// notification is closed and would reject the call.
    Q_INVOKABLE void invokeAction(uint id, const QString& actionKey);
    /// Zero the badge. Called when the panel opens, since that is what
    /// "read" means here.
    Q_INVOKABLE void markAllRead();

Q_SIGNALS:
    void serverActiveChanged();
    void countChanged();
    void unreadCountChanged();

private:
    struct Entry
    {
        uint id = 0;
        QString appName;
        QString appIcon;
        QString summary;
        QString body;
        QDateTime timestamp;
        int urgency = 1;
        bool live = true;
    };

    void onNotificationAdded(PhosphorServiceNotifications::Notification* notification);
    void onNotificationClosed(uint id);
    /// Re-snapshot a notification that changed under a `replaces_id`
    /// update. Bound per object rather than to a server signal because
    /// there is no server signal: an update mutates the existing
    /// Notification in place and fires ITS `changed()`.
    void refreshEntry(PhosphorServiceNotifications::Notification* notification);
    /// Copy a notification's fields into an entry. The one place the
    /// snapshot is taken, so an arrival and an update cannot disagree
    /// about which fields the centre keeps.
    [[nodiscard]] static Entry snapshot(PhosphorServiceNotifications::Notification* notification);
    /// Row index of `id`, or -1. Linear: the list is capped at a few dozen
    /// entries and is walked only on a close or a user action, so an index
    /// would cost more to maintain than it saves.
    [[nodiscard]] int indexOf(uint id) const;
    void setUnreadCount(int count);

    /// Shared body of both constructors: wires the live model and the
    /// server's signals. Kept out of a delegating constructor because the
    /// two differ only in OWNERSHIP, and duplicating the connects is how
    /// the injected path silently loses one.
    void attachServer();

    // Newest first, so the panel reads top-down without a proxy model.
    QList<Entry> m_entries;
    int m_unread = 0;
    // Non-null for the whole lifetime. m_ownedServer holds it only when
    // this object constructed it; the injected server is owned by the
    // caller and outlives this by contract.
    PhosphorServiceNotifications::NotificationServer* m_server = nullptr;
    std::unique_ptr<PhosphorServiceNotifications::NotificationServer> m_ownedServer;
    std::unique_ptr<PhosphorServiceNotifications::NotificationModel> m_live;
};

} // namespace PhosphorShellApp
