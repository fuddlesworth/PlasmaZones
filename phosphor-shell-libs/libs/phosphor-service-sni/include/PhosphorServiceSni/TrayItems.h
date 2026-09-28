// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorServiceSni/phosphorservicesni_export.h>

#include <QAbstractItemModel>
#include <QObject>
#include <QStringList>
#include <QVariant>

#include <memory>

namespace PhosphorServiceSni {

/// Presentation policy over the StatusNotifierItemModel role contract.
/// Preferences identify an application, while instance keys identify its live
/// D-Bus item. Item objects remain owned by the source's host, never by QML.
class PHOSPHORSERVICESNI_EXPORT TrayItems : public QObject
{
    Q_OBJECT
    Q_DISABLE_COPY_MOVE(TrayItems)
    Q_PROPERTY(QAbstractItemModel* source READ source WRITE setSource NOTIFY sourceChanged)
    Q_PROPERTY(QStringList order READ order WRITE setOrder NOTIFY orderChanged)
    Q_PROPERTY(QVariantMap visibility READ visibility WRITE setVisibility NOTIFY visibilityChanged)
    Q_PROPERTY(int maximumBarIcons READ maximumBarIcons WRITE setMaximumBarIcons NOTIFY maximumBarIconsChanged)
    Q_PROPERTY(
        bool attentionPromotion READ attentionPromotion WRITE setAttentionPromotion NOTIFY attentionPromotionChanged)
    Q_PROPERTY(QVariantList items READ items NOTIFY itemsChanged)
    Q_PROPERTY(QVariantList barItems READ barItems NOTIFY barItemsChanged)
    Q_PROPERTY(QVariantList overflowItems READ overflowItems NOTIFY overflowItemsChanged)

public:
    explicit TrayItems(QObject* parent = nullptr);
    ~TrayItems() override;

    [[nodiscard]] QAbstractItemModel* source() const;
    void setSource(QAbstractItemModel* source);
    [[nodiscard]] QStringList order() const;
    void setOrder(const QStringList& order);
    [[nodiscard]] QVariantMap visibility() const;
    void setVisibility(const QVariantMap& visibility);
    [[nodiscard]] int maximumBarIcons() const;
    void setMaximumBarIcons(int count);
    [[nodiscard]] bool attentionPromotion() const;
    void setAttentionPromotion(bool enabled);

    [[nodiscard]] QVariantList items() const;
    [[nodiscard]] QVariantList barItems() const;
    [[nodiscard]] QVariantList overflowItems() const;
    Q_INVOKABLE QVariantMap itemByInstanceKey(const QString& key) const;

Q_SIGNALS:
    void sourceChanged();
    void orderChanged();
    void visibilityChanged();
    void maximumBarIconsChanged();
    void attentionPromotionChanged();
    void itemsChanged();
    void barItemsChanged();
    void overflowItemsChanged();

private:
    class Private;
    std::unique_ptr<Private> d;

    void rebuild();
    void publish(const QVariantList& items);
    void disconnectItems();
    void forgetItem(QObject* item);
};

} // namespace PhosphorServiceSni
