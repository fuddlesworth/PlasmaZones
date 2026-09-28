// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorServiceSni/TrayItems.h>

#include <PhosphorServiceSni/StatusNotifierItem.h>

#include <QHash>
#include <QPersistentModelIndex>
#include <QPointer>
#include <QQmlEngine>
#include <QSet>

#include <algorithm>
#include <utility>

namespace PhosphorServiceSni {

namespace {

QString preferenceKey(const QVariantMap& item)
{
    const QString id = item.value(QLatin1String("itemId")).toString().trimmed();
    if (!id.isEmpty())
        return QStringLiteral("id:") + id;
    // Unique bus owners change on every launch. Do not persist them in an
    // application's visibility or order preferences.
    return QStringLiteral("fallback:") + item.value(QLatin1String("title")).toString().trimmed().toCaseFolded()
        + QLatin1Char('|') + item.value(QLatin1String("dbusPath")).toString();
}

QString instanceKey(const QVariantMap& item)
{
    return item.value(QLatin1String("dbusService")).toString() + QLatin1Char('|')
        + item.value(QLatin1String("dbusPath")).toString();
}

} // namespace

class TrayItems::Private
{
public:
    QPointer<QAbstractItemModel> source;
    QStringList order;
    QVariantMap visibility;
    int maximumBarIcons = 2;
    bool attentionPromotion = true;
    QVariantList items;
    QVariantList barItems;
    QVariantList overflowItems;
    QHash<QObject*, QMetaObject::Connection> itemConnections;
    QHash<QObject*, QList<QPersistentModelIndex>> itemIndexes;
    QHash<QPersistentModelIndex, QObject*> destroyedItems;
};

TrayItems::TrayItems(QObject* parent)
    : QObject(parent)
    , d(std::make_unique<Private>())
{
}

TrayItems::~TrayItems() = default;

QAbstractItemModel* TrayItems::source() const
{
    return d->source;
}

void TrayItems::setSource(QAbstractItemModel* source)
{
    if (d->source == source)
        return;
    if (d->source)
        disconnect(d->source, nullptr, this, nullptr);
    disconnectItems();
    d->source = source;
    if (source) {
        connect(source, &QAbstractItemModel::rowsInserted, this, &TrayItems::rebuild);
        connect(source, &QAbstractItemModel::rowsRemoved, this, &TrayItems::rebuild);
        connect(source, &QAbstractItemModel::rowsMoved, this, &TrayItems::rebuild);
        connect(source, &QAbstractItemModel::modelReset, this, [this] {
            d->destroyedItems.clear();
            rebuild();
        });
        connect(source, &QAbstractItemModel::dataChanged, this,
                [this](const QModelIndex& first, const QModelIndex& last, const QList<int>& roles) {
                    const int itemRole = d->source->roleNames().key(QByteArrayLiteral("item"), -1);
                    if (roles.isEmpty() || roles.contains(itemRole)) {
                        // The source explicitly published a replacement object.
                        // It can reuse the same allocation and row as its predecessor.
                        for (auto it = d->destroyedItems.begin(); it != d->destroyedItems.end();) {
                            if (it.key().parent() == first.parent() && it.key().row() >= first.row()
                                && it.key().row() <= last.row()) {
                                it = d->destroyedItems.erase(it);
                            } else {
                                ++it;
                            }
                        }
                    }
                    rebuild();
                });
        connect(source, &QAbstractItemModel::layoutChanged, this, &TrayItems::rebuild);
        connect(source, &QObject::destroyed, this, [this] {
            d->source = nullptr;
            disconnectItems();
            publish({});
            Q_EMIT sourceChanged();
        });
    }
    rebuild();
    Q_EMIT sourceChanged();
}

QStringList TrayItems::order() const
{
    return d->order;
}

void TrayItems::setOrder(const QStringList& order)
{
    QStringList normalized;
    for (const QString& key : order) {
        if (!key.isEmpty() && !normalized.contains(key))
            normalized.append(key);
    }
    if (d->order == normalized)
        return;
    d->order = normalized;
    rebuild();
    Q_EMIT orderChanged();
}

QVariantMap TrayItems::visibility() const
{
    return d->visibility;
}

void TrayItems::setVisibility(const QVariantMap& visibility)
{
    QVariantMap normalized;
    for (auto it = visibility.cbegin(); it != visibility.cend(); ++it) {
        const QString policy = it.value().toString();
        if (!it.key().isEmpty()
            && (policy == QLatin1String("pinned") || policy == QLatin1String("auto")
                || policy == QLatin1String("overflow") || policy == QLatin1String("hidden"))) {
            normalized.insert(it.key(), policy);
        }
    }
    if (d->visibility == normalized)
        return;
    d->visibility = normalized;
    rebuild();
    Q_EMIT visibilityChanged();
}

int TrayItems::maximumBarIcons() const
{
    return d->maximumBarIcons;
}

void TrayItems::setMaximumBarIcons(int count)
{
    count = std::clamp(count, 0, 4);
    if (d->maximumBarIcons == count)
        return;
    d->maximumBarIcons = count;
    publish(d->items);
    Q_EMIT maximumBarIconsChanged();
}

bool TrayItems::attentionPromotion() const
{
    return d->attentionPromotion;
}

void TrayItems::setAttentionPromotion(bool enabled)
{
    if (d->attentionPromotion == enabled)
        return;
    d->attentionPromotion = enabled;
    publish(d->items);
    Q_EMIT attentionPromotionChanged();
}

QVariantList TrayItems::items() const
{
    return d->items;
}

QVariantList TrayItems::barItems() const
{
    return d->barItems;
}

QVariantList TrayItems::overflowItems() const
{
    return d->overflowItems;
}

QVariantMap TrayItems::itemByInstanceKey(const QString& key) const
{
    for (const QVariant& value : d->items) {
        const auto item = value.toMap();
        if (item.value(QLatin1String("instanceKey")).toString() == key)
            return item;
    }
    return {};
}

void TrayItems::disconnectItems()
{
    for (const auto& connection : std::as_const(d->itemConnections))
        disconnect(connection);
    d->itemConnections.clear();
    d->itemIndexes.clear();
    d->destroyedItems.clear();
}

void TrayItems::forgetItem(QObject* item)
{
    d->itemConnections.remove(item);
    for (const auto& index : d->itemIndexes.take(item))
        d->destroyedItems.insert(index, item);
    QVariantList remaining;
    for (const QVariant& value : std::as_const(d->items)) {
        if (value.toMap().value(QLatin1String("item")).value<QObject*>() != item)
            remaining.append(value);
    }
    // A source may remove its row after destroying its object. Do not query
    // that source while its removal transaction is still in progress.
    publish(remaining);
}

void TrayItems::rebuild()
{
    QVariantList items;
    QSet<QObject*> liveObjects;
    QSet<QPersistentModelIndex> sourceIndexes;
    QHash<QObject*, QList<QPersistentModelIndex>> itemIndexes;
    if (d->source) {
        const auto roles = d->source->roleNames();
        const int itemRole = roles.key(QByteArrayLiteral("item"), -1);
        for (int row = 0; row < d->source->rowCount(); ++row) {
            const QModelIndex index = d->source->index(row, 0);
            const QPersistentModelIndex persistentIndex(index);
            sourceIndexes.insert(persistentIndex);
            auto* object = itemRole < 0 ? nullptr : d->source->data(index, itemRole).value<QObject*>();
            // A dead object's address may already belong to a new item in a
            // different row. Only suppress the original row's stale pointer.
            if (d->destroyedItems.value(persistentIndex) != object)
                d->destroyedItems.remove(persistentIndex);
            if (!object || d->destroyedItems.contains(persistentIndex))
                continue;
            QVariantMap item;
            for (auto it = roles.cbegin(); it != roles.cend(); ++it)
                item.insert(QString::fromUtf8(it.value()), d->source->data(index, it.key()));
            liveObjects.insert(object);
            itemIndexes[object].append(persistentIndex);
            if (!d->itemConnections.contains(object)) {
                // Returning objects through an invokable must not give the
                // JavaScript garbage collector ownership of host-owned items.
                QQmlEngine::setObjectOwnership(object, QQmlEngine::CppOwnership);
                d->itemConnections.insert(object, connect(object, &QObject::destroyed, this, [this, object] {
                                              forgetItem(object);
                                          }));
            }
            const QString key = preferenceKey(item);
            item.insert(QStringLiteral("preferenceKey"), key);
            item.insert(QStringLiteral("instanceKey"), instanceKey(item));
            item.insert(QStringLiteral("visibility"), d->visibility.value(key, QStringLiteral("auto")));
            item.insert(QStringLiteral("attention"),
                        item.value(QLatin1String("status")).toInt()
                            == static_cast<int>(StatusNotifierItem::Status::NeedsAttention));
            items.append(item);
        }
    }
    for (auto it = d->itemConnections.begin(); it != d->itemConnections.end();) {
        if (!liveObjects.contains(it.key())) {
            disconnect(it.value());
            it = d->itemConnections.erase(it);
        } else {
            ++it;
        }
    }
    d->itemIndexes = itemIndexes;
    for (auto it = d->destroyedItems.begin(); it != d->destroyedItems.end();) {
        if (!it.key().isValid() || !sourceIndexes.contains(it.key()))
            it = d->destroyedItems.erase(it);
        else
            ++it;
    }
    QHash<QString, int> positions;
    for (int i = 0; i < d->order.size(); ++i)
        positions.insert(d->order.at(i), i);
    std::stable_sort(items.begin(), items.end(), [&positions](const QVariant& left, const QVariant& right) {
        const auto position = [&positions](const QVariant& value) {
            return positions.value(value.toMap().value(QLatin1String("preferenceKey")).toString(), positions.size());
        };
        return position(left) < position(right);
    });
    publish(items);
}

void TrayItems::publish(const QVariantList& items)
{
    QVariantList barItems;
    QVariantList overflowItems;
    QSet<QString> inBar;
    const auto appendToBar = [&](const QVariantMap& item) {
        const QString instance = item.value(QLatin1String("instanceKey")).toString();
        if (barItems.size() < d->maximumBarIcons && !inBar.contains(instance)) {
            barItems.append(item);
            inBar.insert(instance);
        }
    };
    if (d->attentionPromotion) {
        for (const QVariant& value : items) {
            const auto item = value.toMap();
            const QString policy = item.value(QLatin1String("visibility")).toString();
            if (item.value(QLatin1String("attention")).toBool() && policy != QLatin1String("hidden")
                && policy != QLatin1String("overflow")) {
                appendToBar(item);
            }
        }
    }
    for (const QVariant& value : items) {
        const auto item = value.toMap();
        if (item.value(QLatin1String("visibility")).toString() == QLatin1String("pinned"))
            appendToBar(item);
    }
    for (const QVariant& value : items) {
        const auto item = value.toMap();
        if (item.value(QLatin1String("visibility")).toString() != QLatin1String("hidden")
            && !inBar.contains(item.value(QLatin1String("instanceKey")).toString())) {
            overflowItems.append(item);
        }
    }
    const bool allChanged = d->items != items;
    const bool barChanged = d->barItems != barItems;
    const bool overflowChanged = d->overflowItems != overflowItems;
    d->items = items;
    d->barItems = barItems;
    d->overflowItems = overflowItems;
    if (allChanged)
        Q_EMIT itemsChanged();
    if (barChanged)
        Q_EMIT barItemsChanged();
    if (overflowChanged)
        Q_EMIT overflowItemsChanged();
}

} // namespace PhosphorServiceSni
