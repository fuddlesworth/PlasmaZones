// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorShell/phosphorshell_export.h>

#include <QList>
#include <QPointer>
#include <QQuickItem>

#include <memory>

QT_BEGIN_NAMESPACE
class QAbstractListModel;
class QModelIndex;
class QQmlComponent;
QT_END_NAMESPACE

namespace PhosphorShell {

class PHOSPHORSHELL_EXPORT Variants : public QQuickItem
{
    Q_OBJECT
    Q_CLASSINFO("DefaultProperty", "delegate")

    Q_PROPERTY(QAbstractListModel* model READ model WRITE setModel NOTIFY modelChanged)
    Q_PROPERTY(QQmlComponent* delegate READ delegate WRITE setDelegate NOTIFY delegateChanged)

public:
    explicit Variants(QQuickItem* parent = nullptr);
    ~Variants() override;

    [[nodiscard]] QAbstractListModel* model() const;
    void setModel(QAbstractListModel* model);

    [[nodiscard]] QQmlComponent* delegate() const;
    void setDelegate(QQmlComponent* delegate);

Q_SIGNALS:
    void modelChanged();
    void delegateChanged();

private Q_SLOTS:
    void onRowsInserted(const QModelIndex& parent, int first, int last);
    void onRowsRemoved(const QModelIndex& parent, int first, int last);
    void onRowsMoved(const QModelIndex& sourceParent, int sourceStart, int sourceEnd, const QModelIndex& destParent,
                     int destRow);
    void onModelReset();
    void onDataChanged(const QModelIndex& topLeft, const QModelIndex& bottomRight, const QList<int>& roles);

private:
    void rebuild();
    void clear();
    void refreshInstanceData(int row);
    QVariantMap buildModelData(int row) const;

    // QPointer so external destruction (model swapped out, delegate
    // component reloaded) doesn't leave us with dangling pointers.
    QPointer<QAbstractListModel> m_model;
    QPointer<QQmlComponent> m_delegate;
    // Each entry holds the delegate-instantiated object. We own them and
    // delete via deleteLater() in clear() to be safe inside QML callstacks.
    QList<QObject*> m_instances;
};

} // namespace PhosphorShell
