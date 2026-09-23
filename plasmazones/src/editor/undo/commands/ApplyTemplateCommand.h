// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "BaseZoneCommand.h"
#include <QVariantList>
#include <QString>

namespace PlasmaZones {

/**
 * @brief Command for applying a template layout
 */
class ApplyTemplateCommand : public BaseZoneCommand
{
public:
    explicit ApplyTemplateCommand(QPointer<ZoneManager> zoneManager, const QString& templateType,
                                  const QVariantList& oldZones, const QVariantList& newZones, const QString& text,
                                  QUndoCommand* parent = nullptr);

    void undo() override;
    void redo() override;
    int id() const override
    {
        return -1;
    } // No merging

private:
    QString m_templateType;
    QVariantList m_oldZones; // Complete zones list before template
    QVariantList m_newZones; // Complete zones list after template
};

} // namespace PlasmaZones
