// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorServiceNotifications/NotificationModel.h>

namespace PhosphorServiceNotifications {

NotificationModel::NotificationModel(QObject* parent)
    : QAbstractListModel(parent)
{
}

NotificationModel::~NotificationModel() = default;

NotificationServer* NotificationModel::server() const
{
    return m_server;
}

void NotificationModel::setServer(NotificationServer* server)
{
    if (m_server == server)
        return;

    const int previousCount = static_cast<int>(m_rows.size());
    beginResetModel();
    if (m_server) {
        disconnect(m_server, nullptr, this, nullptr);
        // The surviving notifications belong to the old server, so their
        // connections to this model would otherwise leak.
        for (auto* notification : std::as_const(m_rows))
            disconnect(notification, nullptr, this, nullptr);
    }
    m_server = server;
    m_rows.clear();
    if (m_server) {
        m_rows = m_server->notifications();
        for (auto* notification : std::as_const(m_rows))
            connectNotification(notification);
        connect(m_server, &NotificationServer::notificationAdded, this, [this](Notification* notification) {
            onNotificationAdded(notification);
        });
        connect(m_server, &NotificationServer::NotificationClosed, this, [this](uint id, uint) {
            onNotificationClosed(id);
        });
        // The server owns the notifications; if it is destroyed while still set,
        // m_server and every m_rows entry would dangle. Drop them all.
        connect(m_server, &QObject::destroyed, this, [this]() {
            const int prev = static_cast<int>(m_rows.size());
            beginResetModel();
            m_rows.clear();
            m_server = nullptr;
            endResetModel();
            Q_EMIT serverChanged();
            if (prev != 0)
                Q_EMIT countChanged();
        });
    }
    endResetModel();
    Q_EMIT serverChanged();
    if (previousCount != static_cast<int>(m_rows.size()))
        Q_EMIT countChanged();
}

int NotificationModel::rowCount(const QModelIndex& parent) const
{
    if (parent.isValid())
        return 0;
    return static_cast<int>(m_rows.size());
}

QVariant NotificationModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_rows.size())
        return {};
    auto* notification = m_rows.at(index.row());
    if (!notification)
        return {};
    switch (role) {
    case NotificationRole:
        return QVariant::fromValue<QObject*>(notification);
    case IdRole:
        return notification->id();
    case AppNameRole:
        return notification->appName();
    case AppIconRole:
        return notification->appIcon();
    case SummaryRole:
        return notification->summary();
    case BodyRole:
        return notification->body();
    case ActionsRole:
        return notification->actions();
    case UrgencyRole:
        return static_cast<int>(notification->urgency());
    case CategoryRole:
        return notification->category();
    case DesktopEntryRole:
        return notification->desktopEntry();
    case ImageRole:
        return notification->image();
    case HasImageRole:
        return notification->hasImage();
    case ResidentRole:
        return notification->resident();
    case TransientRole:
        return notification->transient();
    case SuppressSoundRole:
        return notification->suppressSound();
    case ValueRole:
        return notification->value();
    case ExpireTimeoutRole:
        return notification->expireTimeout();
    case TimestampRole:
        return notification->timestamp();
    default:
        return {};
    }
}

QHash<int, QByteArray> NotificationModel::roleNames() const
{
    return {
        {NotificationRole, "notification"},
        {IdRole, "id"},
        {AppNameRole, "appName"},
        {AppIconRole, "appIcon"},
        {SummaryRole, "summary"},
        {BodyRole, "body"},
        {ActionsRole, "actions"},
        {UrgencyRole, "urgency"},
        {CategoryRole, "category"},
        {DesktopEntryRole, "desktopEntry"},
        {ImageRole, "image"},
        {HasImageRole, "hasImage"},
        {ResidentRole, "resident"},
        {TransientRole, "transient"},
        {SuppressSoundRole, "suppressSound"},
        {ValueRole, "value"},
        {ExpireTimeoutRole, "expireTimeout"},
        {TimestampRole, "timestamp"},
    };
}

void NotificationModel::onNotificationAdded(Notification* notification)
{
    if (!notification || m_rows.contains(notification))
        return;
    // Insert at the ascending-id position the server vends from (its QMap),
    // rather than assuming the new id is always the largest. Ids are monotonic in
    // the common case (so this lands at the end), but allocateId() reuses a freed
    // low id after a uint wrap; finding the slot keeps the model's order matching
    // notifications() unconditionally. m_rows is small, so a linear scan matches
    // the close path's lookup.
    int row = 0;
    while (row < m_rows.size() && m_rows.at(row)->id() < notification->id())
        ++row;
    beginInsertRows({}, row, row);
    m_rows.insert(row, notification);
    connectNotification(notification);
    endInsertRows();
    Q_EMIT countChanged();
}

void NotificationModel::onNotificationClosed(uint id)
{
    int row = -1;
    for (int i = 0; i < m_rows.size(); ++i) {
        if (m_rows.at(i)->id() == id) {
            row = i;
            break;
        }
    }
    if (row < 0)
        return;
    // The server emits NotificationClosed before deleting the object, so the
    // pointer is still valid here; disconnect it before dropping the row.
    disconnect(m_rows.at(row), nullptr, this, nullptr);
    beginRemoveRows({}, row, row);
    m_rows.removeAt(row);
    endRemoveRows();
    Q_EMIT countChanged();
}

void NotificationModel::onNotificationChanged(Notification* notification)
{
    const int row = static_cast<int>(m_rows.indexOf(notification));
    if (row < 0)
        return;
    // A replaces_id update can touch any field, so report all roles changed.
    const auto idx = index(row);
    Q_EMIT dataChanged(idx, idx);
}

void NotificationModel::connectNotification(Notification* notification)
{
    connect(notification, &Notification::changed, this, [this, notification]() {
        onNotificationChanged(notification);
    });
}

} // namespace PhosphorServiceNotifications
