// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QAbstractListModel>
#include <QDateTime>
#include <QImage>
#include <QSet>
#include <QVariantMap>
#include <memory>

namespace PhosphorServiceNotifications {
class Notification;
class NotificationModel;
class NotificationServer;
}
namespace PhosphorShellApp {
// Retained, bounded snapshots. Server objects disappear on expiry; history,
// including pictures and read state, remains until explicitly cleared.
class NotificationController : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(bool doNotDisturb READ doNotDisturb WRITE setDoNotDisturb NOTIFY doNotDisturbChanged)
    Q_PROPERTY(bool popupsSuppressed READ popupsSuppressed WRITE setPopupsSuppressed NOTIFY popupsSuppressedChanged)
    Q_PROPERTY(bool serverActive READ isServerActive NOTIFY serverActiveChanged)
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)
    Q_PROPERTY(int unreadCount READ unreadCount NOTIFY unreadCountChanged)
    Q_PROPERTY(bool canUndo READ canUndo NOTIFY entriesChanged)
public:
    enum Roles {
        IdRole = Qt::UserRole + 1,
        AppNameRole,
        AppIconRole,
        SummaryRole,
        BodyRole,
        TimestampRole,
        UrgencyRole,
        LiveRole,
        UnreadRole,
        PayloadRole
    };
    Q_ENUM(Roles)
    explicit NotificationController(QObject* parent = nullptr);
    NotificationController(PhosphorServiceNotifications::NotificationServer* server, QObject* parent = nullptr);
    ~NotificationController() override;
    bool isServerActive() const;
    bool doNotDisturb() const
    {
        return m_doNotDisturb;
    }
    void setDoNotDisturb(bool enabled);
    bool popupsSuppressed() const
    {
        return m_popupsSuppressed;
    }
    void setPopupsSuppressed(bool suppressed);
    int unreadCount() const;
    bool canUndo() const
    {
        return !m_undo.isEmpty();
    }
    int rowCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;
    PhosphorServiceNotifications::NotificationModel* liveModel() const;
    Q_INVOKABLE uint send(const QString& summary, const QString& body);
    Q_INVOKABLE void dismiss(uint id);
    Q_INVOKABLE void dismissPopup(uint id);
    Q_INVOKABLE void clear();
    Q_INVOKABLE void clearGroup(const QString& key);
    Q_INVOKABLE void undoClear();
    Q_INVOKABLE void invokeAction(uint id, const QString& actionKey, const QString& activationToken = QString());
    Q_INVOKABLE bool reply(uint id, const QString& text);
    Q_INVOKABLE void setExpiryPaused(uint id, bool paused);
    Q_INVOKABLE void markRead(uint id);
    Q_INVOKABLE void markAllRead();
    // Stable keys let the view reconcile rows without destroying an expanded
    // message or focused reply when another app sends a notification.
    Q_INVOKABLE QVariantList presentation(bool unreadOnly, bool byApp, const QStringList& expandedGroups) const;
Q_SIGNALS:
    void notificationArrived(const QVariantMap& notification);
    void notificationUpdated(const QVariantMap& notification);
    void notificationRemoved(uint id);
    void dismissAllArrivals();
    void entriesChanged();
    void doNotDisturbChanged();
    void popupsSuppressedChanged();
    void serverActiveChanged();
    void countChanged();
    void unreadCountChanged();

private:
    struct Entry
    {
        uint id = 0;
        QString appName, appIcon, appKey, summary, body, replyPlaceholder, replyLabel;
        QDateTime timestamp;
        QVariantList actions;
        QImage image;
        int urgency = 1;
        bool live = true, unread = true, transient = false, skipGrouping = false;
    };
    void attachServer();
    void onNotificationAdded(PhosphorServiceNotifications::Notification* notification);
    void onNotificationClosed(uint id);
    void refreshEntry(PhosphorServiceNotifications::Notification* notification);
    Entry snapshot(PhosphorServiceNotifications::Notification* notification);
    QVariantMap payload(const Entry& entry) const;
    QString imageKey(uint id) const;
    int indexOf(uint id) const;
    void changed();
    void removeEntries(const QList<uint>& ids);
    void trim();
    QList<Entry> m_entries, m_undo;
    QSet<uint> m_images;
    int m_unread = 0;
    bool m_doNotDisturb = false, m_popupsSuppressed = false;
    PhosphorServiceNotifications::NotificationServer* m_server = nullptr;
    std::unique_ptr<PhosphorServiceNotifications::NotificationServer> m_ownedServer;
    std::unique_ptr<PhosphorServiceNotifications::NotificationModel> m_live;
};
}
