// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "BaseZoneCommand.h"
#include <QVariantList>
#include <QVariantMap>
#include <QString>

namespace PlasmaZones {

/**
 * @brief Command for deleting a zone with auto-fill (expand neighbors)
 */
class DeleteZoneWithFillCommand : public BaseZoneCommand
{
public:
    explicit DeleteZoneWithFillCommand(QPointer<ZoneManager> zoneManager, const QString& zoneId,
                                       const QVariantMap& deletedZoneData, const QVariantList& oldZones,
                                       const QVariantList& newZones, const QString& text,
                                       QUndoCommand* parent = nullptr);

    void undo() override;
    void redo() override;
    int id() const override
    {
        return -1;
    } // No merging

private:
    QString m_zoneId;
    QVariantMap m_deletedZoneData;
    QVariantList m_oldZones; // Complete zones list before operation
    QVariantList m_newZones; // Complete zones list after operation
};

} // namespace PlasmaZones
