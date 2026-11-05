// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QObject>
#include <QVariantList>
#include <QString>
#include <QUuid>

namespace PlasmaZones {

/**
 * @brief Base class for template strategies
 *
 * Follows Strategy pattern to allow easy extension of template types.
 * Each template type has its own strategy implementation.
 */
class TemplateStrategy : public QObject
{
    Q_OBJECT

public:
    explicit TemplateStrategy(QObject* parent = nullptr)
        : QObject(parent)
    {
    }
    virtual ~TemplateStrategy() = default;

    /**
     * @brief Creates zones for this template type
     * @param columns Number of columns (template-specific)
     * @param rows Number of rows (template-specific)
     * @return List of zone maps
     */
    virtual QVariantList createZones(int columns, int rows) = 0;

    /**
     * @brief Returns the template type name
     */
    virtual QString templateType() const = 0;
};

/**
 * @brief Grid template strategy - creates NxM grid of zones
 */
class GridTemplateStrategy : public TemplateStrategy
{
public:
    explicit GridTemplateStrategy(QObject* parent = nullptr)
        : TemplateStrategy(parent)
    {
    }
    QVariantList createZones(int columns, int rows) override;
    QString templateType() const override
    {
        return QStringLiteral("grid");
    }
};

/**
 * @brief Columns template strategy - creates N vertical columns
 */
class ColumnsTemplateStrategy : public TemplateStrategy
{
public:
    explicit ColumnsTemplateStrategy(QObject* parent = nullptr)
        : TemplateStrategy(parent)
    {
    }
    QVariantList createZones(int columns, int rows) override;
    QString templateType() const override
    {
        return QStringLiteral("columns");
    }
};

/**
 * @brief Rows template strategy - creates N horizontal rows
 */
class RowsTemplateStrategy : public TemplateStrategy
{
public:
    explicit RowsTemplateStrategy(QObject* parent = nullptr)
        : TemplateStrategy(parent)
    {
    }
    QVariantList createZones(int columns, int rows) override;
    QString templateType() const override
    {
        return QStringLiteral("rows");
    }
};

/**
 * @brief Priority grid template strategy - main area + 2 secondary stacked
 */
class PriorityTemplateStrategy : public TemplateStrategy
{
public:
    explicit PriorityTemplateStrategy(QObject* parent = nullptr)
        : TemplateStrategy(parent)
    {
    }
    QVariantList createZones(int columns, int rows) override;
    QString templateType() const override
    {
        return QStringLiteral("priority");
    }
};

/**
 * @brief Focus template strategy - side panels + large center
 */
class FocusTemplateStrategy : public TemplateStrategy
{
public:
    explicit FocusTemplateStrategy(QObject* parent = nullptr)
        : TemplateStrategy(parent)
    {
    }
    QVariantList createZones(int columns, int rows) override;
    QString templateType() const override
    {
        return QStringLiteral("focus");
    }
};

} // namespace PlasmaZones
