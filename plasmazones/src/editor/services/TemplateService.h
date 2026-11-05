// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "TemplateStrategy.h"
#include <QObject>
#include <QVariantList>
#include <QMap>

namespace PlasmaZones {

/**
 * @brief Service for applying layout templates
 *
 * Uses Strategy pattern to support different template types.
 * Easy to extend with new template types by adding new strategies.
 */
class TemplateService : public QObject
{
    Q_OBJECT

public:
    explicit TemplateService(QObject* parent = nullptr);
    ~TemplateService() override;

    /**
     * @brief Applies a template and returns the created zones
     * @param templateType The type of template (grid, columns, rows, priority, focus)
     * @param columns Number of columns (for grid/columns templates)
     * @param rows Number of rows (for grid/rows templates)
     * @return List of zone maps, or empty list on failure
     */
    QVariantList applyTemplate(const QString& templateType, int columns = 2, int rows = 2);

    /**
     * @brief Gets list of available template types
     */
    QStringList availableTemplates() const;

private:
    QMap<QString, TemplateStrategy*> m_strategies;
};

} // namespace PlasmaZones
