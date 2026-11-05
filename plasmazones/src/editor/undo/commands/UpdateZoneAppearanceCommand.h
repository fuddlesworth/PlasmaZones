// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "BaseZoneCommand.h"
#include "CommandId.h"
#include <QVariant>
#include <QString>

namespace PlasmaZones {

/**
 * @brief Command for updating zone appearance (colors, opacity, border, etc.)
 */
class UpdateZoneAppearanceCommand : public BaseZoneCommand
{
public:
    explicit UpdateZoneAppearanceCommand(QPointer<ZoneManager> zoneManager, const QString& zoneId,
                                         const QString& propertyName, const QVariant& oldValue,
                                         const QVariant& newValue, const QString& text, QUndoCommand* parent = nullptr);

    void undo() override;
    void redo() override;
    int id() const override
    {
        return CommandId::UpdateAppearance;
    }
    bool mergeWith(const QUndoCommand* other) override;

private:
    QString m_zoneId;
    QString m_propertyName;
    QVariant m_oldValue;
    QVariant m_newValue;
};

} // namespace PlasmaZones
