// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "BaseZoneCommand.h"
#include <QString>
#include <QVariantMap>

namespace PlasmaZones {

/**
 * @brief Command for duplicating a zone
 */
class DuplicateZoneCommand : public BaseZoneCommand
{
public:
    explicit DuplicateZoneCommand(QPointer<ZoneManager> zoneManager, const QString& sourceZoneId,
                                  const QString& duplicatedZoneId, const QVariantMap& duplicatedZoneData,
                                  const QString& text, QUndoCommand* parent = nullptr);

    void undo() override;
    void redo() override;
    int id() const override
    {
        return -1;
    } // No merging

private:
    QString m_sourceZoneId;
    QString m_duplicatedZoneId;
    QVariantMap m_duplicatedZoneData; // Complete zone data for restoration
};

} // namespace PlasmaZones
