// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "BaseZoneCommand.h"
#include <QVariantMap>
#include <QString>

namespace PlasmaZones {

/**
 * @brief Command for adding a zone
 */
class AddZoneCommand : public BaseZoneCommand
{
public:
    explicit AddZoneCommand(QPointer<ZoneManager> zoneManager, const QString& zoneId, const QVariantMap& zoneData,
                            const QString& text, QUndoCommand* parent = nullptr);

    void undo() override;
    void redo() override;
    int id() const override
    {
        return -1;
    } // No merging

private:
    QString m_zoneId; // Zone ID (provided at construction)
    QVariantMap m_zoneData; // Complete zone data for restoration
};

} // namespace PlasmaZones
