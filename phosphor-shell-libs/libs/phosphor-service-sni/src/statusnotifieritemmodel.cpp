// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorServiceSni/StatusNotifierItemModel.h>

#include <PhosphorServiceSni/StatusNotifierHost.h>
#include <PhosphorServiceSni/StatusNotifierItem.h>

#include <PhosphorServiceIconTheme/IconImageProvider.h>
#include <PhosphorServiceIconTheme/QmlRegistration.h>

#include <QHash>
#include <QImage>
#include <QLoggingCategory>
#include <QPointer>
#include <QUrl>
#include <QVariant>

#include <array>

Q_LOGGING_CATEGORY(lcSniModel, "phosphor.service.sni.model")

namespace PhosphorServiceSni {

namespace {

constexpr int kVariantNone = 0;
constexpr int kVariantOverlay = 1;
constexpr int kVariantAttention = 2;

/// Provider key for an item's icon variant. Plain path-form
/// (service|path[#variant]), NO query string. Qt's image-provider
/// dispatch strips the URL's query component before calling
/// requestImage(), so the provider only ever sees this path-form key.
QString iconKeyBase(const StatusNotifierItem* item, int variant)
{
    QString k = item->dbusService() + QLatin1Char('|') + item->dbusPath();
    switch (variant) {
    case kVariantOverlay:
        k += QStringLiteral("#overlay");
        break;
    case kVariantAttention:
        k += QStringLiteral("#attention");
        break;
    default:
        break;
    }
    return k;
}

QImage variantImage(const StatusNotifierItem* item, int variant)
{
    switch (variant) {
    case kVariantOverlay:
        return item->overlayIconImage();
    case kVariantAttention:
        return item->attentionIconImage();
    default:
        return item->iconImage();
    }
}

/// Build the QML URL for `key` against the current image-provider
/// host. Routed through `imageProviderUrlHost()` so a future rename
/// on the icontheme side forces a link failure rather than a silent
/// "image provider not found" at runtime.
QString providerUrl(const QString& key, qint64 cacheKey)
{
    return QStringLiteral("image://") + QString::fromLatin1(PhosphorServiceIconTheme::imageProviderUrlHost())
        + QLatin1Char('/') + key + QStringLiteral("?v=") + QString::number(cacheKey);
}

/// Cache of published URLs per item variant. Held by the model's
/// Private so a QML data() query is a hash lookup rather than the
/// previous re-publish on every read (which was both wasteful and
/// caused IconImageProvider's static registry to churn).
struct IconUrlCache
{
    std::array<QString, 3> url{}; ///< one slot per variant
    std::array<QString, 3> publishedKey{}; ///< what was setImage'd; used to clearImage on remove
};

} // namespace

class StatusNotifierItemModel::Private
{
public:
    QPointer<StatusNotifierHost> host;
    // Mirrors host->items() in row order so rowFor / itemAt avoid the
    // host's by-value QList copy and stay O(1) on add/remove.
    QList<StatusNotifierItem*> items;
    QHash<StatusNotifierItem*, IconUrlCache> iconUrls;

    void publishAll(StatusNotifierItem* item)
    {
        auto& slot = iconUrls[item];
        for (int v = 0; v < 3; ++v) {
            republishVariant(item, v, slot);
        }
    }

    // Recompute the URL for one variant if the QImage actually
    // changed. Returns true when the cached URL string moved.
    bool republishVariant(StatusNotifierItem* item, int variant, IconUrlCache& slot)
    {
        const QImage img = variantImage(item, variant);
        const QString key = iconKeyBase(item, variant);

        if (img.isNull()) {
            // No payload; drop both the registry slot and the URL.
            if (!slot.publishedKey[variant].isEmpty()) {
                PhosphorServiceIconTheme::IconImageProvider::clearImage(slot.publishedKey[variant]);
                slot.publishedKey[variant].clear();
            }
            if (slot.url[variant].isEmpty())
                return false;
            slot.url[variant].clear();
            qCDebug(lcSniModel) << "no icon image for" << key << "; clearing URL";
            return true;
        }

        const QString newUrl = providerUrl(key, img.cacheKey());
        if (newUrl == slot.url[variant])
            return false;

        PhosphorServiceIconTheme::IconImageProvider::setImage(key, img);
        slot.publishedKey[variant] = key;
        slot.url[variant] = newUrl;
        qCDebug(lcSniModel) << "publishing icon for" << key << "size" << img.size() << "cacheKey" << img.cacheKey();
        return true;
    }

    void clearItem(StatusNotifierItem* item)
    {
        const auto it = iconUrls.constFind(item);
        if (it == iconUrls.constEnd())
            return;
        for (int v = 0; v < 3; ++v) {
            if (!it->publishedKey[v].isEmpty()) {
                PhosphorServiceIconTheme::IconImageProvider::clearImage(it->publishedKey[v]);
            }
        }
        iconUrls.erase(it);
    }

    // Drop every published key. Used by the host-destroyed lambda and
    // by ~StatusNotifierItemModel; the model destructor must clear
    // because IconImageProvider's static registry would otherwise
    // accumulate zombie entries if the model is destroyed while its
    // host is still alive (uncommon outside tests, but the cleanup
    // is cheap and the pre-existing leak was a real one).
    void clearAllPublished()
    {
        for (auto it = iconUrls.constBegin(); it != iconUrls.constEnd(); ++it) {
            for (int v = 0; v < 3; ++v) {
                if (!it->publishedKey[v].isEmpty())
                    PhosphorServiceIconTheme::IconImageProvider::clearImage(it->publishedKey[v]);
            }
        }
        iconUrls.clear();
    }
};

StatusNotifierItemModel::StatusNotifierItemModel(QObject* parent)
    : QAbstractListModel(parent)
    , d(std::make_unique<Private>())
{
}

StatusNotifierItemModel::~StatusNotifierItemModel()
{
    // Release every key still held in IconImageProvider's static
    // registry. The host-destroyed lambda and setHost(nullptr) paths
    // also call clearAllPublished, but a model destroyed while its
    // host is still alive (test fixtures, embedded scenarios) would
    // otherwise leak entries.
    d->clearAllPublished();
}

StatusNotifierHost* StatusNotifierItemModel::host() const
{
    return d->host;
}

void StatusNotifierItemModel::setHost(StatusNotifierHost* host)
{
    if (d->host == host)
        return;

    const int previousCount = d->items.size();

    if (d->host) {
        // Only bracket the disconnect/clear with begin/endResetModel
        // when there's actually a row to remove. Empty-to-empty
        // transitions otherwise fire a spurious modelReset that every
        // attached ListView treats as a hard refresh.
        const bool hadRows = !d->items.isEmpty();
        if (hadRows)
            beginResetModel();
        disconnect(d->host, nullptr, this, nullptr);
        for (auto* item : std::as_const(d->items)) {
            disconnect(item, nullptr, this, nullptr);
            d->clearItem(item);
        }
        d->items.clear();
        if (hadRows)
            endResetModel();
    }

    d->host = host;

    if (d->host) {
        // Snapshot the host items first; only fire begin/endResetModel
        // when there's a real row insertion to announce. Empty hosts
        // skip the reset entirely so QML views attached to an empty
        // model don't see a no-op refresh.
        auto incoming = d->host->items();
        if (!incoming.isEmpty()) {
            beginResetModel();
            d->items = std::move(incoming);
            for (auto* item : std::as_const(d->items)) {
                connectItem(item);
                d->publishAll(item);
            }
            endResetModel();
        }

        connect(d->host, &StatusNotifierHost::itemAdded, this, &StatusNotifierItemModel::onItemAdded);
        connect(d->host, &StatusNotifierHost::itemRemoved, this, &StatusNotifierItemModel::onItemRemoved);
        // Host destruction nulls the QPointer automatically, but the
        // model's mirrored item list (and the icon-URL cache) would
        // still hold dangling pointers to the host's parent-owned
        // children. Reset both to leave QML observers with a clean
        // empty model rather than a crash on next data().
        connect(d->host, &QObject::destroyed, this, [this]() {
            const int prev = d->items.size();
            // Mirror the begin/endResetModel gating used in setHost: an
            // empty-to-empty transition (host attached but never
            // populated) does not need a modelReset, which would
            // otherwise fire a no-op refresh on every attached ListView.
            if (prev != 0) {
                beginResetModel();
                d->items.clear();
                d->clearAllPublished();
                endResetModel();
            }
            Q_EMIT hostChanged();
            if (prev != 0)
                Q_EMIT countChanged();
        });
    }

    Q_EMIT hostChanged();
    // Emit countChanged only when the row count actually moved; the
    // CLAUDE.md "only emit when value changed" rule applies even to
    // attach/detach paths because spurious notifications cause QML
    // bindings that read `count` to re-evaluate dependent expressions.
    if (previousCount != d->items.size()) {
        Q_EMIT countChanged();
    }
}

void StatusNotifierItemModel::connectItem(StatusNotifierItem* item)
{
    // Property-change signals trigger a per-row dataChanged so QML
    // delegates refresh just that row. iconChanged is handled
    // separately because it also updates the published URL cache.
    auto refresh = [this, item] {
        onItemDataChanged(item);
    };
    connect(item, &StatusNotifierItem::titleChanged, this, refresh);
    connect(item, &StatusNotifierItem::categoryChanged, this, refresh);
    connect(item, &StatusNotifierItem::statusChanged, this, refresh);
    connect(item, &StatusNotifierItem::toolTipChanged, this, refresh);
    connect(item, &StatusNotifierItem::menuPathChanged, this, refresh);
    connect(item, &StatusNotifierItem::idChanged, this, refresh);
    connect(item, &StatusNotifierItem::iconChanged, this, [this, item] {
        onItemIconChanged(item);
    });
}

int StatusNotifierItemModel::rowCount(const QModelIndex& parent) const
{
    if (parent.isValid())
        return 0;
    return d->items.size();
}

QVariant StatusNotifierItemModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= d->items.size())
        return {};
    auto* item = d->items.at(index.row());
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
        return d->iconUrls.value(item).url[kVariantNone];
    case OverlayIconUrlRole:
        return d->iconUrls.value(item).url[kVariantOverlay];
    case AttentionIconUrlRole:
        return d->iconUrls.value(item).url[kVariantAttention];
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
        // URL forms: bind these to QML `Image.source`.
        {IconUrlRole, "iconUrl"},
        {OverlayIconUrlRole, "overlayIconUrl"},
        {AttentionIconUrlRole, "attentionIconUrl"},
        // Raw-QImage forms: keep for C++ / future ImageItem bindings.
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
    if (!item || d->items.contains(item))
        return;
    const int row = d->items.size();
    beginInsertRows({}, row, row);
    d->items.append(item);
    connectItem(item);
    d->publishAll(item);
    endInsertRows();
    Q_EMIT countChanged();
}

void StatusNotifierItemModel::onItemRemoved(StatusNotifierItem* item)
{
    const int row = d->items.indexOf(item);
    if (row < 0)
        return;
    beginRemoveRows({}, row, row);
    disconnect(item, nullptr, this, nullptr);
    d->clearItem(item);
    d->items.removeAt(row);
    endRemoveRows();
    Q_EMIT countChanged();
}

void StatusNotifierItemModel::onItemDataChanged(StatusNotifierItem* item)
{
    const int row = d->items.indexOf(item);
    if (row < 0)
        return;
    const auto idx = index(row);
    Q_EMIT dataChanged(idx, idx);
}

void StatusNotifierItemModel::onItemIconChanged(StatusNotifierItem* item)
{
    const int row = d->items.indexOf(item);
    if (row < 0)
        return;
    auto& slot = d->iconUrls[item];
    bool anyChanged = false;
    for (int v = 0; v < 3; ++v) {
        anyChanged = d->republishVariant(item, v, slot) || anyChanged;
    }
    if (!anyChanged)
        return;
    const auto idx = index(row);
    Q_EMIT dataChanged(idx, idx,
                       {IconUrlRole, OverlayIconUrlRole, AttentionIconUrlRole, IconImageRole, OverlayIconImageRole,
                        AttentionIconImageRole});
}

int StatusNotifierItemModel::rowFor(StatusNotifierItem* item) const
{
    return d->items.indexOf(item);
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
    if (row < 0 || row >= d->items.size())
        return nullptr;
    return d->items.at(row);
}

} // namespace PhosphorServiceSni
