// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "BaseZoneCommand.h"
#include "CommandId.h"
#include <QRectF>
#include <QString>

namespace PlasmaZones {

/**
 * @brief Command for updating zone geometry
 */
class UpdateZoneGeometryCommand : public BaseZoneCommand
{
public:
    explicit UpdateZoneGeometryCommand(QPointer<ZoneManager> zoneManager, const QString& zoneId,
                                       const QRectF& oldGeometry, const QRectF& newGeometry, const QString& text,
                                       QUndoCommand* parent = nullptr);

    void undo() override;
    void redo() override;
    int id() const override
    {
        return CommandId::UpdateGeometry;
    }
    bool mergeWith(const QUndoCommand* other) override;

private:
    QString m_zoneId;
    QRectF m_oldGeometry;
    QRectF m_newGeometry;
};

} // namespace PlasmaZones
