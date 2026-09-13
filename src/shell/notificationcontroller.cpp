// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "NotificationController.h"

#include <PhosphorServiceNotifications/Notification.h>
#include <PhosphorServiceNotifications/NotificationModel.h>
#include <PhosphorServiceNotifications/NotificationServer.h>

#include <QLoggingCategory>

Q_LOGGING_CATEGORY(lcNotificationCentre, "phosphor.shell.notifications")

using namespace PhosphorServiceNotifications;

namespace PhosphorShellApp {

namespace {
// How many entries the centre keeps. Past this the oldest is dropped.
// A cap rather than unbounded growth: a chatty application left running
// for a week would otherwise hold every notification it ever sent in
// memory, and nobody scrolls back that far.
constexpr int kMaxEntries = 50;
} // namespace

NotificationController::NotificationController(QObject* parent)
    : NotificationController(nullptr, parent)
{
}

NotificationController::NotificationController(NotificationServer* server, QObject* parent)
    : QAbstractListModel(parent)
    , m_live(std::make_unique<NotificationModel>())
{
    if (server) {
        m_server = server;
    } else {
        m_ownedServer = std::make_unique<NotificationServer>();
        m_server = m_ownedServer.get();
    }
    attachServer();
}

void NotificationController::attachServer()
{
    m_live->setServer(m_server);

    if (!m_server->nameAcquired()) {
        // Not fatal, and not a bug in this process: another notification
        // daemon owns the name. Said once, at startup, so an empty centre
        // has an explanation in the journal.
        qCInfo(lcNotificationCentre)
            << "another daemon owns org.freedesktop.Notifications; the notification centre stays empty";
    }

    connect(m_server, &NotificationServer::nameAcquiredChanged, this, &NotificationController::serverActiveChanged);
    connect(m_server, &NotificationServer::notificationAdded, this, &NotificationController::onNotificationAdded);
    connect(m_server, &NotificationServer::NotificationClosed, this, [this](uint id, uint) {
        onNotificationClosed(id);
    });
}

NotificationController::~NotificationController() = default;

bool NotificationController::isServerActive() const
{
    return m_server->nameAcquired();
}

int NotificationController::unreadCount() const
{
    return m_unread;
}

NotificationModel* NotificationController::liveModel() const
{
    return m_live.get();
}

int NotificationController::rowCount(const QModelIndex& parent) const
{
    // A list model has rows only under the root index; a valid parent means
    // the caller is walking a tree that is not there.
    if (parent.isValid()) {
        return 0;
    }
    return static_cast<int>(m_entries.size());
}

QVariant NotificationController::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_entries.size()) {
        return {};
    }
    const Entry& entry = m_entries.at(index.row());
    switch (role) {
    case IdRole:
        return entry.id;
    case AppNameRole:
        return entry.appName;
    case AppIconRole:
        return entry.appIcon;
    case SummaryRole:
        return entry.summary;
    case BodyRole:
        return entry.body;
    case TimestampRole:
        return entry.timestamp;
    case UrgencyRole:
        return entry.urgency;
    case LiveRole:
        return entry.live;
    default:
        return {};
    }
}

QHash<int, QByteArray> NotificationController::roleNames() const
{
    static const QHash<int, QByteArray> kRoles = {
        {IdRole, QByteArrayLiteral("notificationId")}, {AppNameRole, QByteArrayLiteral("appName")},
        {AppIconRole, QByteArrayLiteral("appIcon")},   {SummaryRole, QByteArrayLiteral("summary")},
        {BodyRole, QByteArrayLiteral("body")},         {TimestampRole, QByteArrayLiteral("timestamp")},
        {UrgencyRole, QByteArrayLiteral("urgency")},   {LiveRole, QByteArrayLiteral("live")},
    };
    return kRoles;
}

NotificationController::Entry NotificationController::snapshot(Notification* notification)
{
    Entry entry;
    entry.id = notification->id();
    entry.appName = notification->appName();
    entry.appIcon = notification->appIcon();
    entry.summary = notification->summary();
    entry.body = notification->body();
    entry.timestamp = notification->timestamp();
    entry.urgency = static_cast<int>(notification->urgency());
    entry.live = true;
    return entry;
}

void NotificationController::onNotificationAdded(Notification* notification)
{
    if (!notification) {
        return;
    }
    // A `replaces_id` update does NOT arrive here. The server re-applies
    // the field set to the EXISTING object and fires that object's
    // changed(), so the update path is a per-object connection rather than
    // an id check on this one. Without it a progress notification (one id,
    // many updates) would sit in the centre at whatever text it first had.
    //
    // Context object is `this`, so the connection dies with the controller;
    // the notification itself is destroyed by the server on close, which
    // drops it from the other side.
    connect(notification, &Notification::changed, this, [this, notification] {
        refreshEntry(notification);
    });

    const Entry entry = snapshot(notification);

    beginInsertRows({}, 0, 0);
    m_entries.prepend(entry);
    endInsertRows();

    if (m_entries.size() > kMaxEntries) {
        const int last = static_cast<int>(m_entries.size()) - 1;
        beginRemoveRows({}, last, last);
        m_entries.removeLast();
        endRemoveRows();
    }

    Q_EMIT countChanged();
    setUnreadCount(m_unread + 1);
    // The toast is fed from HERE rather than from the ingest path, so the
    // stack and the centre can never show different sets.
    Q_EMIT notificationArrived(entry.summary, entry.body);
}

void NotificationController::refreshEntry(Notification* notification)
{
    if (!notification) {
        return;
    }
    const int row = indexOf(notification->id());
    if (row < 0) {
        // The entry was dismissed from the centre while the sending
        // application went on updating it. Nothing to refresh, and adding
        // it back would resurrect a row the user explicitly cleared.
        return;
    }
    // `live` is NOT taken from the snapshot here: an update to a
    // notification the user already dismissed from the centre would
    // otherwise flip a dead row back to live. The server only re-applies
    // fields to an object it still holds, so the flag stays whatever the
    // close path last set it to.
    const bool wasLive = m_entries.at(row).live;
    m_entries[row] = snapshot(notification);
    m_entries[row].live = wasLive;
    const QModelIndex idx = index(row, 0);
    Q_EMIT dataChanged(idx, idx);
}

void NotificationController::onNotificationClosed(uint id)
{
    const int row = indexOf(id);
    if (row < 0) {
        return;
    }
    // The entry STAYS; only its live flag drops. This is the whole point of
    // the centre: a notification that expired is exactly what the user came
    // here to read. Removal is the user's call (dismiss / clear).
    if (!m_entries[row].live) {
        return;
    }
    m_entries[row].live = false;
    const QModelIndex idx = index(row, 0);
    Q_EMIT dataChanged(idx, idx, {LiveRole});
}

void NotificationController::dismiss(uint id)
{
    const int row = indexOf(id);
    if (row < 0) {
        return;
    }
    // Close server-side FIRST, while the row is still here: closing emits
    // NotificationClosed, which routes back through onNotificationClosed and
    // needs to find the entry to clear its flag. Removing first would leave
    // that a silent no-op — harmless today, and a trap the moment the close
    // path grows any other side effect.
    if (m_entries.at(row).live) {
        m_server->dismissNotification(id);
    }
    beginRemoveRows({}, row, row);
    m_entries.removeAt(row);
    endRemoveRows();
    Q_EMIT countChanged();
}

void NotificationController::clear()
{
    if (m_entries.isEmpty()) {
        return;
    }
    // Collect the live ids before touching the list: dismissNotification
    // re-enters through NotificationClosed, and mutating the list under an
    // iterator walking it is how that becomes a crash instead of a bug.
    QList<uint> liveIds;
    for (const Entry& entry : m_entries) {
        if (entry.live) {
            liveIds.append(entry.id);
        }
    }

    beginResetModel();
    m_entries.clear();
    endResetModel();

    for (const uint id : liveIds) {
        m_server->dismissNotification(id);
    }

    Q_EMIT countChanged();
    setUnreadCount(0);
}

void NotificationController::invokeAction(uint id, const QString& actionKey)
{
    const int row = indexOf(id);
    if (row < 0 || !m_entries.at(row).live) {
        // The sending application has already been told this notification
        // closed; invoking an action on it would be answered by nothing.
        qCDebug(lcNotificationCentre) << "action" << actionKey << "on notification" << id
                                      << "the server no longer holds; ignoring";
        return;
    }
    m_server->invokeAction(id, actionKey);
}

void NotificationController::markAllRead()
{
    setUnreadCount(0);
}

int NotificationController::indexOf(uint id) const
{
    for (int i = 0; i < m_entries.size(); ++i) {
        if (m_entries.at(i).id == id) {
            return i;
        }
    }
    return -1;
}

void NotificationController::setUnreadCount(int count)
{
    if (m_unread == count) {
        return;
    }
    m_unread = count;
    Q_EMIT unreadCountChanged();
}

} // namespace PhosphorShellApp
