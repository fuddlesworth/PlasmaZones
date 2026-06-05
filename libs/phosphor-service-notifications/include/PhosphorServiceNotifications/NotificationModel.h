// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorServiceNotifications/phosphorservicenotifications_export.h>

#include <PhosphorServiceNotifications/Notification.h>
#include <PhosphorServiceNotifications/NotificationServer.h>

#include <QAbstractListModel>
#include <QList>

namespace PhosphorServiceNotifications {

/// List model over a `NotificationServer`'s live notifications. Bind `server`,
/// then drive a `ListView` / `Repeater` off the rows. Rows are kept in
/// ascending-id order (the same order the server vends): a new notification is
/// inserted at its id-sorted position, a `replaces_id` update mutates the row in
/// place and forwards as `dataChanged`, and a close removes the row. The model
/// does NOT own the notifications; the server does.
class PHOSPHORSERVICENOTIFICATIONS_EXPORT NotificationModel : public QAbstractListModel
{
    Q_OBJECT
    Q_DISABLE_COPY_MOVE(NotificationModel)
    Q_PROPERTY(
        PhosphorServiceNotifications::NotificationServer* server READ server WRITE setServer NOTIFY serverChanged)
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)

public:
    enum Roles {
        NotificationRole = Qt::UserRole + 1,
        IdRole,
        AppNameRole,
        AppIconRole,
        SummaryRole,
        BodyRole,
        ActionsRole,
        UrgencyRole,
        CategoryRole,
        DesktopEntryRole,
        ImageRole,
        HasImageRole,
        ResidentRole,
        TransientRole,
        SuppressSoundRole,
        ValueRole,
        ExpireTimeoutRole,
        TimestampRole,
    };
    Q_ENUM(Roles)

    explicit NotificationModel(QObject* parent = nullptr);
    ~NotificationModel() override;

    [[nodiscard]] NotificationServer* server() const;
    void setServer(NotificationServer* server);

    [[nodiscard]] int rowCount(const QModelIndex& parent = {}) const override;
    [[nodiscard]] QVariant data(const QModelIndex& index, int role) const override;
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;

Q_SIGNALS:
    void serverChanged();
    void countChanged();

private:
    void connectNotification(Notification* notification);
    void onNotificationAdded(Notification* notification);
    void onNotificationClosed(uint id);
    void onNotificationChanged(Notification* notification);

    NotificationServer* m_server = nullptr;
    // Row mirror of server-owned Notification pointers; the model does NOT own
    // them. The server keeps each alive until its NotificationClosed fires, at
    // which point the row is removed before the object is deleted.
    QList<Notification*> m_rows;
};

} // namespace PhosphorServiceNotifications
