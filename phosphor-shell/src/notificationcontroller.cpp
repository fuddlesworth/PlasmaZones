// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
#include "NotificationController.h"
#include <PhosphorServiceNotifications/Notification.h>
#include <PhosphorServiceNotifications/NotificationModel.h>
#include <PhosphorServiceNotifications/NotificationServer.h>
#include <PhosphorServiceIconTheme/IconImageProvider.h>
#include <QCoreApplication>
#include <QLocale>
#include <QGuiApplication>
#ifdef PHOSPHOR_SHELL_HAVE_KWINDOWSYSTEM
#include <KWaylandExtras>
#include <kwindowsystem_version.h>
#endif
#include <algorithm>

using namespace PhosphorServiceNotifications;
using PhosphorServiceIconTheme::IconImageProvider;
namespace PhosphorShellApp {
namespace {
constexpr int kMaxEntries = 50;
}
NotificationController::NotificationController(QObject* parent)
    : NotificationController(nullptr, parent)
{
}
NotificationController::NotificationController(NotificationServer* server, QObject* parent)
    : QAbstractListModel(parent)
    , m_live(std::make_unique<NotificationModel>())
{
    if (server)
        m_server = server;
    else {
        m_ownedServer = std::make_unique<NotificationServer>();
        m_server = m_ownedServer.get();
    }
    attachServer();
}
void NotificationController::attachServer()
{
    m_live->setServer(m_server);
    connect(m_server, &NotificationServer::nameAcquiredChanged, this, &NotificationController::serverActiveChanged);
    connect(m_server, &NotificationServer::notificationAdded, this, &NotificationController::onNotificationAdded);
    connect(m_server, &NotificationServer::NotificationClosed, this, [this](uint id, uint) {
        onNotificationClosed(id);
    });
    for (auto* notification : m_server->notifications())
        onNotificationAdded(notification);
}
NotificationController::~NotificationController()
{
    for (uint id : m_images)
        IconImageProvider::clearImage(imageKey(id));
}
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
    return parent.isValid() ? 0 : m_entries.size();
}
QVariant NotificationController::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_entries.size())
        return {};
    const auto& e = m_entries.at(index.row());
    switch (role) {
    case IdRole:
        return e.id;
    case AppNameRole:
        return e.appName;
    case AppIconRole:
        return e.appIcon;
    case SummaryRole:
        return e.summary;
    case BodyRole:
        return e.body;
    case TimestampRole:
        return e.timestamp;
    case UrgencyRole:
        return e.urgency;
    case LiveRole:
        return e.live;
    case UnreadRole:
        return e.unread;
    case PayloadRole:
        return payload(e);
    default:
        return {};
    }
}
QHash<int, QByteArray> NotificationController::roleNames() const
{
    return {{IdRole, "notificationId"}, {AppNameRole, "appName"}, {AppIconRole, "appIcon"},
            {SummaryRole, "summary"},   {BodyRole, "body"},       {TimestampRole, "timestamp"},
            {UrgencyRole, "urgency"},   {LiveRole, "live"},       {UnreadRole, "unread"},
            {PayloadRole, "payload"}};
}
QString NotificationController::imageKey(uint id) const
{
    return QStringLiteral("notification-%1-%2").arg(quintptr(this), 0, 16).arg(id);
}
NotificationController::Entry NotificationController::snapshot(Notification* n)
{
    Entry e;
    e.id = n->id();
    e.appName = n->appName().isEmpty() ? tr("Notification") : n->appName();
    e.appIcon = n->appIcon().isEmpty() ? n->desktopEntry() : n->appIcon();
    e.appKey = n->desktopEntry().isEmpty() ? e.appName : n->desktopEntry();
    e.summary = n->summary();
    e.body = n->body();
    e.timestamp = n->timestamp();
    e.urgency = n->urgency();
    e.transient = n->transient();
    e.skipGrouping = n->hints().value(QStringLiteral("x-kde-skipGrouping")).toBool();
    e.replyPlaceholder = n->hints().value(QStringLiteral("x-kde-reply-placeholder-text")).toString();
    e.replyLabel = n->hints().value(QStringLiteral("x-kde-reply-submit-button-text")).toString();
    const auto actions = n->actions();
    for (int i = 0; i + 1 < actions.size(); i += 2)
        e.actions.append(QVariantMap{{QStringLiteral("key"), actions[i]}, {QStringLiteral("label"), actions[i + 1]}});
    e.image = n->image();
    if (e.image.width() > 1024 || e.image.height() > 1024)
        e.image = e.image.scaled(1024, 1024, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    if (!e.image.isNull()) {
        IconImageProvider::setImage(imageKey(e.id), e.image);
        m_images.insert(e.id);
    } else {
        IconImageProvider::clearImage(imageKey(e.id));
        m_images.remove(e.id);
    }
    return e;
}
QVariantMap NotificationController::payload(const Entry& e) const
{
    // The image registry outlives the server object. Revisioned URLs invalidate
    // Qt's image cache on replaces_id without changing the card's identity.
    const QString image = e.image.isNull()
        ? QString()
        : QStringLiteral("image://phosphor-service-icontheme/%1?v=%2").arg(imageKey(e.id)).arg(e.image.cacheKey());
    uint hue = 0;
    for (QChar c : e.appKey)
        hue = hue * 31 + c.unicode();
    return {{QStringLiteral("id"), e.id},
            {QStringLiteral("appName"), e.appName},
            {QStringLiteral("appIcon"), e.appIcon},
            {QStringLiteral("appKey"), e.appKey},
            {QStringLiteral("summary"), e.summary},
            {QStringLiteral("body"), e.body},
            {QStringLiteral("timestamp"), e.timestamp},
            {QStringLiteral("urgency"), e.urgency},
            {QStringLiteral("live"), e.live},
            {QStringLiteral("unread"), e.unread},
            {QStringLiteral("transient"), e.transient},
            {QStringLiteral("imageSource"), image},
            {QStringLiteral("imageWidth"), e.image.width()},
            {QStringLiteral("imageHeight"), e.image.height()},
            {QStringLiteral("actions"), e.live ? e.actions : QVariantList{}},
            {QStringLiteral("replyPlaceholder"), e.replyPlaceholder},
            {QStringLiteral("replyLabel"), e.replyLabel},
            {QStringLiteral("colorIndex"), e.urgency == 2 ? 3 : int(hue % 3)},
            {QStringLiteral("managed"), true},
            {QStringLiteral("timeout"), 0}};
}
void NotificationController::setDoNotDisturb(bool enabled)
{
    if (m_doNotDisturb == enabled)
        return;
    m_doNotDisturb = enabled;
    if (enabled)
        Q_EMIT dismissAllArrivals();
    Q_EMIT doNotDisturbChanged();
}
void NotificationController::setPopupsSuppressed(bool suppressed)
{
    if (m_popupsSuppressed == suppressed)
        return;
    m_popupsSuppressed = suppressed;
    if (suppressed)
        Q_EMIT dismissAllArrivals();
    Q_EMIT popupsSuppressedChanged();
}
uint NotificationController::send(const QString& summary, const QString& body)
{
    return m_server->Notify(QStringLiteral("Phosphor"), 0, QStringLiteral("notifications"), summary, body, {}, {}, -1);
}
void NotificationController::onNotificationAdded(Notification* notification)
{
    if (!notification)
        return;
    connect(notification, &Notification::changed, this, [this, notification] {
        refreshEntry(notification);
    });
    const Entry entry = snapshot(notification);
    beginInsertRows({}, 0, 0);
    m_entries.prepend(entry);
    endInsertRows();
    trim();
    changed();
    if (!m_doNotDisturb && !m_popupsSuppressed)
        Q_EMIT notificationArrived(payload(entry));
}
void NotificationController::refreshEntry(Notification* notification)
{
    const int row = indexOf(notification->id());
    if (row < 0)
        return;
    const bool unread = m_entries[row].unread;
    m_entries[row] = snapshot(notification);
    m_entries[row].unread = unread;
    Q_EMIT dataChanged(index(row), index(row));
    Q_EMIT notificationUpdated(payload(m_entries[row]));
    changed();
}
void NotificationController::onNotificationClosed(uint id)
{
    Q_EMIT notificationRemoved(id);
    const int row = indexOf(id);
    if (row < 0)
        return;
    if (m_entries[row].transient) {
        beginRemoveRows({}, row, row);
        m_entries.removeAt(row);
        endRemoveRows();
    } else {
        m_entries[row].live = false;
        Q_EMIT dataChanged(index(row), index(row), {LiveRole, PayloadRole});
    }
    changed();
}
void NotificationController::dismissPopup(uint id)
{
    m_server->dismissNotification(id);
}
void NotificationController::dismiss(uint id)
{
    if (indexOf(id) >= 0)
        removeEntries({id});
}
void NotificationController::clear()
{
    QList<uint> ids;
    for (const auto& e : m_entries)
        ids.append(e.id);
    if (!ids.isEmpty())
        removeEntries(ids);
}
void NotificationController::clearGroup(const QString& key)
{
    QList<uint> ids;
    for (const auto& e : m_entries)
        if (e.appKey == key)
            ids.append(e.id);
    if (!ids.isEmpty())
        removeEntries(ids);
}
void NotificationController::removeEntries(const QList<uint>& ids)
{
    m_undo.clear();
    for (uint id : ids) {
        int row = indexOf(id);
        if (row < 0)
            continue;
        Entry entry = m_entries[row];
        entry.live = false;
        if (!entry.transient)
            m_undo.append(entry);
        beginRemoveRows({}, row, row);
        m_entries.removeAt(row);
        endRemoveRows();
        m_server->dismissNotification(id);
    }
    changed();
}
void NotificationController::undoClear()
{
    if (m_undo.isEmpty())
        return;
    beginResetModel();
    for (const auto& entry : m_undo)
        if (indexOf(entry.id) < 0)
            m_entries.append(entry);
    m_undo.clear();
    std::stable_sort(m_entries.begin(), m_entries.end(), [](const Entry& a, const Entry& b) {
        return a.id > b.id;
    });
    while (m_entries.size() > kMaxEntries)
        m_entries.removeLast();
    endResetModel();
    changed();
}
void NotificationController::invokeAction(uint id, const QString& key, const QString& token)
{
    if (indexOf(id) < 0)
        return;
    markRead(id);
    m_server->invokeAction(id, key, token);
}
void NotificationController::activate(uint id, const QString& key, QWindow* window)
{
    const int row = indexOf(id);
    if (row < 0 || !m_entries[row].live)
        return;
#ifdef PHOSPHOR_SHELL_HAVE_KWINDOWSYSTEM
#if KWINDOWSYSTEM_VERSION >= QT_VERSION_CHECK(6, 19, 0)
    if (window && QGuiApplication::platformName().startsWith(QLatin1String("wayland"))) {
        setExpiryPaused(id, true);
        KWaylandExtras::xdgActivationToken(window, m_entries[row].appKey)
            .then(this, [this, id, key](const QString& token) {
                setExpiryPaused(id, false);
                invokeAction(id, key, token);
            });
        return;
    }
#endif
#endif
    Q_UNUSED(window)
    invokeAction(id, key);
}
bool NotificationController::reply(uint id, const QString& text)
{
    if (!m_server->reply(id, text))
        return false;
    markRead(id);
    return true;
}
void NotificationController::setExpiryPaused(uint id, bool paused)
{
    m_server->setExpiryPaused(id, paused);
}
void NotificationController::markRead(uint id)
{
    const int row = indexOf(id);
    if (row < 0 || !m_entries[row].unread)
        return;
    m_entries[row].unread = false;
    Q_EMIT dataChanged(index(row), index(row), {UnreadRole, PayloadRole});
    Q_EMIT notificationUpdated(payload(m_entries[row]));
    changed();
}
void NotificationController::markAllRead()
{
    if (!m_unread)
        return;
    for (auto& entry : m_entries)
        entry.unread = false;
    Q_EMIT dataChanged(index(0), index(m_entries.size() - 1), {UnreadRole, PayloadRole});
    changed();
}
int NotificationController::indexOf(uint id) const
{
    for (int i = 0; i < m_entries.size(); ++i)
        if (m_entries[i].id == id)
            return i;
    return -1;
}
void NotificationController::trim()
{
    while (m_entries.size() > kMaxEntries) {
        const auto entry = m_entries.last();
        const int row = m_entries.size() - 1;
        beginRemoveRows({}, row, row);
        m_entries.removeLast();
        endRemoveRows();
        m_server->dismissNotification(entry.id);
    }
}
void NotificationController::changed()
{
    const int unread = std::count_if(m_entries.cbegin(), m_entries.cend(), [](const Entry& e) {
        return e.unread;
    });
    if (m_unread != unread) {
        m_unread = unread;
        Q_EMIT unreadCountChanged();
    }
    QSet<uint> retained;
    for (const auto& entry : m_entries)
        retained.insert(entry.id);
    for (const auto& entry : m_undo)
        retained.insert(entry.id);
    for (uint id : m_images - retained) {
        IconImageProvider::clearImage(imageKey(id));
        m_images.remove(id);
    }
    Q_EMIT countChanged();
    Q_EMIT entriesChanged();
}
QVariantList NotificationController::presentation(bool unreadOnly, bool byApp, const QStringList& expanded) const
{
    QVariantList rows;
    auto append = [&rows](const QString& kind, const QString& key, const QVariantMap& value) {
        rows.append(QVariantMap{{QStringLiteral("kind"), kind},
                                {QStringLiteral("key"), key},
                                {QStringLiteral("payload"), value}});
    };
    QList<const Entry*> normal, urgent;
    for (const auto& e : m_entries) {
        if (unreadOnly && !e.unread)
            continue;
        (e.urgency == 2 ? urgent : normal).append(&e);
    }
    auto card = [&](const Entry* e) {
        append(QStringLiteral("notification"), QStringLiteral("n%1").arg(e->id), payload(*e));
    };
    if (!urgent.isEmpty()) {
        append(QStringLiteral("section"), QStringLiteral("urgent"), {{QStringLiteral("label"), tr("Needs attention")}});
        for (auto* e : urgent)
            card(e);
    }
    QSet<QString> visited;
    for (const auto* entry : normal) {
        const auto date = entry->timestamp.date();
        const QString dateKey = date.toString(Qt::ISODate);
        if (!visited.contains(dateKey)) {
            visited.insert(dateKey);
            append(QStringLiteral("section"), dateKey,
                   {{QStringLiteral("label"),
                     date == QDate::currentDate() ? tr("Today") : QLocale().toString(date, QLocale::ShortFormat)}});
        }
        if (!byApp || entry->skipGrouping) {
            card(entry);
            continue;
        }
        const QString groupKey = dateKey + QLatin1Char('/') + entry->appKey;
        if (visited.contains(groupKey))
            continue;
        visited.insert(groupKey);
        QList<const Entry*> group;
        for (const auto* other : normal)
            if (!other->skipGrouping && other->appKey == entry->appKey && other->timestamp.date() == date)
                group.append(other);
        auto info = payload(*entry);
        info.insert(QStringLiteral("count"), group.size());
        info.insert(QStringLiteral("groupKey"), groupKey);
        append(QStringLiteral("group"), QStringLiteral("g/") + groupKey, info);
        card(group.first());
        if (expanded.contains(groupKey))
            for (int i = 1; i < group.size(); ++i)
                card(group[i]);
        if (group.size() > 1)
            append(QStringLiteral("earlier"), QStringLiteral("e/") + groupKey, info);
    }
    return rows;
}
}
