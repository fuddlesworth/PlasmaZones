// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "BaseZoneCommand.h"
#include <QString>

namespace PlasmaZones {

/**
 * @brief Command for updating zone name
 */
class UpdateZoneNameCommand : public BaseZoneCommand
{
public:
    explicit UpdateZoneNameCommand(QPointer<ZoneManager> zoneManager, const QString& zoneId, const QString& oldName,
                                   const QString& newName, const QString& text, QUndoCommand* parent = nullptr);

    void undo() override;
    void redo() override;
    int id() const override
    {
        return -1;
    } // No merging

private:
    QString m_zoneId;
    QString m_oldName;
    QString m_newName;
};

} // namespace PlasmaZones
