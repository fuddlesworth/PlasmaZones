// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "BaseZoneCommand.h"
#include <QRectF>
#include <QString>

namespace PlasmaZones {

/**
 * @brief Command for filling a zone (expand to fill space)
 */
class FillZoneCommand : public BaseZoneCommand
{
public:
    explicit FillZoneCommand(QPointer<ZoneManager> zoneManager, const QString& zoneId, const QRectF& oldGeometry,
                             const QRectF& newGeometry, const QString& text, QUndoCommand* parent = nullptr);

    void undo() override;
    void redo() override;
    int id() const override
    {
        return -1;
    } // No merging

private:
    QString m_zoneId;
    QRectF m_oldGeometry;
    QRectF m_newGeometry;
};

} // namespace PlasmaZones
