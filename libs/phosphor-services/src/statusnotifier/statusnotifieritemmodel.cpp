// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorServices/StatusNotifierItemModel.h>

#include <PhosphorServices/StatusNotifierHost.h>
#include <PhosphorServices/StatusNotifierItem.h>

#include "../iconimageprovider.h"

#include <QLoggingCategory>
#include <QUrl>
#include <QVariant>

Q_LOGGING_CATEGORY(lcSniModel, "phosphorservices.sni.model")

namespace PhosphorServices {

namespace {

/// Provider key for an item's icon variant. Plain path-form
/// (service|path[#variant]) — NO query string. Qt's image-provider
/// dispatch strips the URL's query component before calling
/// requestImage(), so the provider only ever sees this path-form key.
/// Earlier rev appended ?v=<cacheKey> to the publish key as well,
/// which made every lookup miss (provider id != publish id).
QString iconKeyBase(const StatusNotifierItem* item, const QString& variant)
{
    QString k = item->dbusService() + QLatin1Char('|') + item->dbusPath();
    if (!variant.isEmpty()) {
        k += QLatin1Char('#') + variant;
    }
    return k;
}

/// Snapshot the current QImage for the given variant, publish it to
/// the image provider under the path-form key, and return a URL with
/// a cacheKey-bearing query string. The query string is invisible to
/// the provider (Qt strips it) but visible to QML's URL comparator,
/// which is what gates whether `Image` re-fetches: same URL → reuse
/// cached pixmap; different URL → call requestImage() again. So
/// every icon-data change yields a new URL → new requestImage() call
/// → fresh QImage from the registry.
QString publishAndUrl(StatusNotifierItem* item, const QString& variant)
{
    if (!item)
        return {};
    const QImage img = variant == QLatin1String("overlay") ? item->overlayIconImage()
        : variant == QLatin1String("attention")            ? item->attentionIconImage()
                                                           : item->iconImage();
    const QString key = iconKeyBase(item, variant);
    if (img.isNull()) {
        // Empty URL = nothing for QML's Image to bind to → tray
        // delegate's transparent Rectangle renders as zero-px wide.
        // The log helps diagnose "tray items exist but I see nothing":
        // it usually means the XDG icon-theme resolver couldn't find
        // the named icon (item shipped IconName but no IconPixmap and
        // no IconThemePath fallback) — pick a generic fallback icon
        // in the QML delegate, OR teach the resolver about the app's
        // theme dir, OR install the app's icon theme system-wide.
        qCDebug(lcSniModel) << "no icon image for" << key
                            << "— Image.source will be empty, delegate will render invisible";
        return {};
    }
    qCDebug(lcSniModel) << "publishing icon for" << key << "size" << img.size() << "cacheKey" << img.cacheKey();
    IconImageProvider::setImage(key, img);
    return QStringLiteral("image://phosphor-services/") + key + QStringLiteral("?v=") + QString::number(img.cacheKey());
}

} // namespace

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
    // setHost wraps a beginResetModel/endResetModel — the model's
    // row count moved from "old host count" to "new host count" but
    // QML's `count` binding doesn't subscribe to modelReset, only to
    // the property's NOTIFY signal. Fire it explicitly so QML
    // recomputes on every host attach.
    Q_EMIT countChanged();
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
    case IconUrlRole:
        return publishAndUrl(item, {});
    case OverlayIconUrlRole:
        return publishAndUrl(item, QStringLiteral("overlay"));
    case AttentionIconUrlRole:
        return publishAndUrl(item, QStringLiteral("attention"));
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
        // URL forms — bind these to QML `Image.source`.
        {IconUrlRole, "iconUrl"},
        {OverlayIconUrlRole, "overlayIconUrl"},
        {AttentionIconUrlRole, "attentionIconUrl"},
        // Raw-QImage forms — keep for C++ / future ImageItem bindings.
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
    Q_EMIT countChanged();
}

void StatusNotifierItemModel::onItemRemoved(StatusNotifierItem* item)
{
    const int row = rowFor(item);
    if (row < 0)
        return;
    // The host emits itemRemoved BEFORE removing the item from its
    // internal list, so rowFor() still finds a valid index here.
    beginRemoveRows({}, row, row);
    disconnect(item, nullptr, this, nullptr);
    endRemoveRows();
    Q_EMIT countChanged();
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
