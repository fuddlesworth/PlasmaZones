// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "BaseZoneCommand.h"
#include "CommandId.h"
#include <QVariantList>
#include <QString>

namespace PlasmaZones {

/**
 * @brief Command for changing zone z-order (bringToFront, sendToBack, bringForward, sendBackward)
 */
class ChangeZOrderCommand : public BaseZoneCommand
{
public:
    explicit ChangeZOrderCommand(QPointer<ZoneManager> zoneManager, const QString& zoneId, const QVariantList& oldZones,
                                 const QVariantList& newZones, const QString& text, QUndoCommand* parent = nullptr);

    void undo() override;
    void redo() override;
    int id() const override
    {
        return CommandId::ChangeZOrder;
    }
    bool mergeWith(const QUndoCommand* other) override;

private:
    QString m_zoneId;
    QVariantList m_oldZones; // Complete zones list before operation
    QVariantList m_newZones; // Complete zones list after operation
};

} // namespace PlasmaZones
