// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "BaseZoneCommand.h"
#include <QVariantMap>
#include <QVariantList>
#include <QString>

namespace PlasmaZones {

/**
 * @brief Command for splitting a zone
 */
class SplitZoneCommand : public BaseZoneCommand
{
public:
    explicit SplitZoneCommand(QPointer<ZoneManager> zoneManager, const QString& originalZoneId,
                              const QVariantMap& originalZoneData, const QVariantList& newZonesData,
                              const QString& text, QUndoCommand* parent = nullptr);

    void undo() override;
    void redo() override;
    int id() const override
    {
        return -1;
    } // No merging

private:
    QString m_originalZoneId;
    QVariantMap m_originalZoneData;
    QVariantList m_newZonesData; // List of zone data (2 zones after split)
};

} // namespace PlasmaZones
