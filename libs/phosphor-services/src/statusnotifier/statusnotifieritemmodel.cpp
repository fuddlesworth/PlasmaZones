// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorServices/StatusNotifierItemModel.h>

#include <PhosphorServices/StatusNotifierHost.h>
#include <PhosphorServices/StatusNotifierItem.h>

#include <QVariant>

namespace PhosphorServices {

StatusNotifierItemModel::StatusNotifierItemModel(QObject* parent)
    : QAbstractListModel(parent)
{
}

StatusNotifierItemModel::~StatusNotifierItemModel() = default;

StatusNotifierHost* StatusNotifierItemModel::host() const
{
    return m_host;
}

void StatusNotifierItemModel::setHost(StatusNotifierHost* host)
{
    if (m_host == host)
        return;

    if (m_host) {
        beginResetModel();
        disconnect(m_host, nullptr, this, nullptr);
        // Disconnect any item-level signals we hooked.
        const auto existing = m_host->items();
        for (auto* item : existing) {
            disconnect(item, nullptr, this, nullptr);
        }
        endResetModel();
    }

    m_host = host;

    if (m_host) {
        beginResetModel();
        const auto existing = m_host->items();
        for (auto* item : existing) {
            connectItem(item);
        }
        endResetModel();

        connect(m_host, &StatusNotifierHost::itemAdded, this, &StatusNotifierItemModel::onItemAdded);
        connect(m_host, &StatusNotifierHost::itemRemoved, this, &StatusNotifierItemModel::onItemRemoved);
    }

    Q_EMIT hostChanged();
}

void StatusNotifierItemModel::connectItem(StatusNotifierItem* item)
{
    // Every property-change signal triggers a per-row dataChanged so
    // QML bindings refresh just that delegate. Connect lambdas
    // because dataChanged() needs the (row, roles) lookup.
    auto refresh = [this, item] {
        onItemDataChanged(item);
    };
    connect(item, &StatusNotifierItem::titleChanged, this, refresh);
    connect(item, &StatusNotifierItem::categoryChanged, this, refresh);
    connect(item, &StatusNotifierItem::statusChanged, this, refresh);
    connect(item, &StatusNotifierItem::iconChanged, this, refresh);
    connect(item, &StatusNotifierItem::toolTipChanged, this, refresh);
    connect(item, &StatusNotifierItem::menuPathChanged, this, refresh);
    connect(item, &StatusNotifierItem::idChanged, this, refresh);
}

int StatusNotifierItemModel::rowCount(const QModelIndex& parent) const
{
    if (parent.isValid() || !m_host)
        return 0;
    return m_host->itemCount();
}

QVariant StatusNotifierItemModel::data(const QModelIndex& index, int role) const
{
    if (!m_host || !index.isValid())
        return {};
    auto* item = m_host->itemAt(index.row());
    if (!item)
        return {};

    switch (role) {
    case IdRole:
        return item->id();
    case TitleRole:
        return item->title();
    case CategoryRole:
        return item->category();
    case StatusRole:
        return QVariant::fromValue(item->status());
    case IconImageRole:
        return item->iconImage();
    case OverlayIconImageRole:
        return item->overlayIconImage();
    case AttentionIconImageRole:
        return item->attentionIconImage();
    case ToolTipTitleRole:
        return item->toolTipTitle();
    case ToolTipBodyRole:
        return item->toolTipBody();
    case MenuPathRole:
        return item->menuPath();
    case ItemIsMenuRole:
        return item->itemIsMenu();
    case DBusServiceRole:
        return item->dbusService();
    case DBusPathRole:
        return item->dbusPath();
    case ItemObjectRole:
        return QVariant::fromValue<QObject*>(item);
    default:
        return {};
    }
}

QHash<int, QByteArray> StatusNotifierItemModel::roleNames() const
{
    return {
        {IdRole, "itemId"},
        {TitleRole, "title"},
        {CategoryRole, "category"},
        {StatusRole, "status"},
        {IconImageRole, "iconImage"},
        {OverlayIconImageRole, "overlayIconImage"},
        {AttentionIconImageRole, "attentionIconImage"},
        {ToolTipTitleRole, "toolTipTitle"},
        {ToolTipBodyRole, "toolTipBody"},
        {MenuPathRole, "menuPath"},
        {ItemIsMenuRole, "itemIsMenu"},
        {DBusServiceRole, "dbusService"},
        {DBusPathRole, "dbusPath"},
        {ItemObjectRole, "item"},
    };
}

void StatusNotifierItemModel::onItemAdded(StatusNotifierItem* item)
{
    const int row = m_host->itemCount() - 1;
    beginInsertRows({}, row, row);
    connectItem(item);
    endInsertRows();
}

void StatusNotifierItemModel::onItemRemoved(StatusNotifierItem* item)
{
    const int row = rowFor(item);
    if (row < 0)
        return;
    // Host hasn't yet erased it from items() — actually, it HAS,
    // because itemRemoved fires before deleteLater(). Recompute from
    // the host snapshot we held.
    beginRemoveRows({}, row, row);
    disconnect(item, nullptr, this, nullptr);
    endRemoveRows();
}

void StatusNotifierItemModel::onItemDataChanged(StatusNotifierItem* item)
{
    const int row = rowFor(item);
    if (row < 0)
        return;
    const auto idx = index(row);
    Q_EMIT dataChanged(idx, idx);
}

int StatusNotifierItemModel::rowFor(StatusNotifierItem* item) const
{
    if (!m_host)
        return -1;
    const auto list = m_host->items();
    return list.indexOf(item);
}

void StatusNotifierItemModel::activate(int row, int x, int y)
{
    if (auto* item = itemAt(row))
        item->activate(x, y);
}

void StatusNotifierItemModel::secondaryActivate(int row, int x, int y)
{
    if (auto* item = itemAt(row))
        item->secondaryActivate(x, y);
}

void StatusNotifierItemModel::contextMenu(int row, int x, int y)
{
    if (auto* item = itemAt(row))
        item->contextMenu(x, y);
}

void StatusNotifierItemModel::scroll(int row, int delta, const QString& orientation)
{
    if (auto* item = itemAt(row))
        item->scroll(delta, orientation);
}

StatusNotifierItem* StatusNotifierItemModel::itemAt(int row) const
{
    if (!m_host)
        return nullptr;
    return m_host->itemAt(row);
}

} // namespace PhosphorServices
