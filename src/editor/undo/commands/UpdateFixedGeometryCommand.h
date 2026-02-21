// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "BaseZoneCommand.h"
#include "CommandId.h"
#include <QRectF>
#include <QString>

namespace PlasmaZones {

/**
 * @brief Command for updating fixed pixel geometry via spinboxes
 *
 * Stores both the fixed pixel coords and the relative fallback so that
 * undo/redo restores both correctly.
 */
class UpdateFixedGeometryCommand : public BaseZoneCommand
{
public:
    explicit UpdateFixedGeometryCommand(QPointer<ZoneManager> zoneManager, const QString& zoneId,
                                        const QRectF& oldFixed, const QRectF& newFixed,
                                        const QRectF& oldRelative, const QRectF& newRelative,
                                        QUndoCommand* parent = nullptr);

    void undo() override;
    void redo() override;
    int id() const override
    {
        return CommandId::UpdateFixedGeometry;
    }
    bool mergeWith(const QUndoCommand* other) override;

private:
    void applyGeometry(const QRectF& fixed, const QRectF& relative);

    QString m_zoneId;
    QRectF m_oldFixed;
    QRectF m_newFixed;
    QRectF m_oldRelative;
    QRectF m_newRelative;
};

} // namespace PlasmaZones
