// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorShell/phosphorshell_export.h>

#include <QAbstractListModel>
#include <QList>
#include <QtQml/qqmlregistration.h>

QT_BEGIN_NAMESPACE
class QScreen;
QT_END_NAMESPACE

namespace PhosphorLayer {
class IScreenProvider;
} // namespace PhosphorLayer

namespace PhosphorShell {

class PHOSPHORSHELL_EXPORT ScreenModel : public QAbstractListModel
{
    Q_OBJECT
    QML_NAMED_ELEMENT(ScreenModel)
    QML_UNCREATABLE("ScreenModel is accessed via PhosphorShell.screens")

public:
    enum Role {
        ScreenRole = Qt::UserRole + 1,
        NameRole,
        WidthRole,
        HeightRole,
        IsPrimaryRole,
    };
    Q_ENUM(Role)

    explicit ScreenModel(PhosphorLayer::IScreenProvider* provider, QObject* parent = nullptr);
    ~ScreenModel() override;

    int rowCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

private Q_SLOTS:
    void onScreensChanged();

private:
    PhosphorLayer::IScreenProvider* m_provider;
    QList<QScreen*> m_screens;
};

} // namespace PhosphorShell
