// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorShell/phosphorshell_export.h>

#include <QQuickItem>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

namespace PhosphorShell {

class PHOSPHORSHELL_EXPORT PersistentProperties : public QQuickItem
{
    Q_OBJECT
    QML_NAMED_ELEMENT(PersistentProperties)

    Q_PROPERTY(QString reloadId READ reloadId WRITE setReloadId NOTIFY reloadIdChanged)

public:
    explicit PersistentProperties(QQuickItem* parent = nullptr);
    ~PersistentProperties() override;

    [[nodiscard]] QString reloadId() const;
    void setReloadId(const QString& id);

    [[nodiscard]] QVariantMap saveState() const;
    void restoreState(const QVariantMap& state);

Q_SIGNALS:
    void reloadIdChanged();

private:
    QString m_reloadId;
};

} // namespace PhosphorShell
