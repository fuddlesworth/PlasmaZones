// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <QObject>
#include <QStringList>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

namespace PhosphorShellDashboard {

class ShortcutReferenceModel : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QVariantList rows READ rows WRITE setRows NOTIFY rowsChanged)
    Q_PROPERTY(QString scope READ scope WRITE setScope NOTIFY scopeChanged)
    Q_PROPERTY(QString query READ query WRITE setQuery NOTIFY queryChanged)
    Q_PROPERTY(bool assignedOnly READ assignedOnly WRITE setAssignedOnly NOTIFY assignedOnlyChanged)
    Q_PROPERTY(bool layoutsAvailable READ layoutsAvailable WRITE setLayoutsAvailable NOTIFY layoutsAvailableChanged)
    Q_PROPERTY(QVariantList groups READ groups NOTIFY groupsChanged)
    Q_PROPERTY(int count READ count NOTIFY countChanged)

public:
    explicit ShortcutReferenceModel(QObject* parent = nullptr);
    QVariantList rows() const
    {
        return m_rows;
    }
    QString scope() const
    {
        return m_scope;
    }
    QString query() const
    {
        return m_query;
    }
    bool assignedOnly() const
    {
        return m_assignedOnly;
    }
    bool layoutsAvailable() const
    {
        return m_layoutsAvailable;
    }
    QVariantList groups() const
    {
        return m_groups;
    }
    int count() const
    {
        return m_count;
    }

    void setRows(const QVariantList& rows);
    void setScope(const QString& scope);
    void setQuery(const QString& query);
    void setAssignedOnly(bool assignedOnly);
    void setLayoutsAvailable(bool available);
    Q_INVOKABLE QStringList keyPartsForTrigger(const QString& trigger) const;

Q_SIGNALS:
    void rowsChanged();
    void scopeChanged();
    void queryChanged();
    void assignedOnlyChanged();
    void layoutsAvailableChanged();
    void groupsChanged();
    void countChanged();

protected:
    bool event(QEvent* event) override;

private:
    void rebuild();
    QVariantList m_rows;
    QVariantList m_groups;
    QString m_scope = QStringLiteral("tiling");
    QString m_query;
    bool m_assignedOnly = false;
    bool m_layoutsAvailable = true;
    int m_count = 0;
};
} // namespace PhosphorShellDashboard
