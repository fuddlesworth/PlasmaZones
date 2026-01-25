// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "BaseZoneCommand.h"
#include <QString>

namespace PlasmaZones {

/**
 * @brief Command for updating a zone's number
 */
class UpdateZoneNumberCommand : public BaseZoneCommand
{
public:
    explicit UpdateZoneNumberCommand(QPointer<ZoneManager> zoneManager, const QString& zoneId, int oldNumber,
                                     int newNumber, const QString& text, QUndoCommand* parent = nullptr);

    void undo() override;
    void redo() override;
    int id() const override
    {
        return -1;
    } // No merging

private:
    QString m_zoneId;
    int m_oldNumber;
    int m_newNumber;
};

} // namespace PlasmaZones
