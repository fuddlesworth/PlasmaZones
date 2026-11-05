// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "BaseZoneCommand.h"
#include <QMap>
#include <QVariant>

namespace PlasmaZones {

/**
 * @brief Command for batch updating appearance properties across multiple zones
 *
 * Updates a single property for multiple zones in one undoable operation.
 * Uses batch update mode to defer signal emission until all changes are complete.
 */
class BatchUpdateAppearanceCommand : public BaseZoneCommand
{
public:
    /**
     * @brief Construct a batch appearance update command
     * @param zoneManager The zone manager
     * @param zoneIds List of zone IDs to update
     * @param propertyName The property to update (e.g., "useCustomColors", "opacity")
     * @param oldValues Map of zone ID -> old value
     * @param newValue The new value to set for all zones
     * @param text Command description for undo stack
     * @param parent Parent command (for macro support)
     */
    BatchUpdateAppearanceCommand(QPointer<ZoneManager> zoneManager, const QStringList& zoneIds,
                                 const QString& propertyName, const QMap<QString, QVariant>& oldValues,
                                 const QVariant& newValue, const QString& text = QString(),
                                 QUndoCommand* parent = nullptr);

    void undo() override;
    void redo() override;

    int id() const override
    {
        return -1;
    } // No merging for batch operations

private:
    QStringList m_zoneIds;
    QString m_propertyName;
    QMap<QString, QVariant> m_oldValues;
    QVariant m_newValue;
};

/**
 * @brief Command for batch updating colors across multiple zones
 *
 * Updates a single color type for multiple zones in one undoable operation.
 * Uses batch update mode to defer signal emission until all changes are complete.
 */
class BatchUpdateColorCommand : public BaseZoneCommand
{
public:
    /**
     * @brief Construct a batch color update command
     * @param zoneManager The zone manager
     * @param zoneIds List of zone IDs to update
     * @param colorType The color type to update (e.g., "highlightColor", "inactiveColor")
     * @param oldColors Map of zone ID -> old color value
     * @param newColor The new color to set for all zones
     * @param text Command description for undo stack
     * @param parent Parent command (for macro support)
     */
    BatchUpdateColorCommand(QPointer<ZoneManager> zoneManager, const QStringList& zoneIds, const QString& colorType,
                            const QMap<QString, QString>& oldColors, const QString& newColor,
                            const QString& text = QString(), QUndoCommand* parent = nullptr);

    void undo() override;
    void redo() override;

    int id() const override
    {
        return -1;
    } // No merging for batch operations

private:
    QStringList m_zoneIds;
    QString m_colorType;
    QMap<QString, QString> m_oldColors;
    QString m_newColor;
};

} // namespace PlasmaZones
