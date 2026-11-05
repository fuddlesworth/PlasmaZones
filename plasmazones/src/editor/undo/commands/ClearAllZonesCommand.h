// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "BaseZoneCommand.h"
#include <QVariantList>

namespace PlasmaZones {

/**
 * @brief Command for clearing all zones
 */
class ClearAllZonesCommand : public BaseZoneCommand
{
public:
    explicit ClearAllZonesCommand(QPointer<ZoneManager> zoneManager, const QVariantList& oldZones, const QString& text,
                                  QUndoCommand* parent = nullptr);

    void undo() override;
    void redo() override;
    int id() const override
    {
        return -1;
    } // No merging

private:
    QVariantList m_oldZones; // Complete zones list for restoration
};

} // namespace PlasmaZones
