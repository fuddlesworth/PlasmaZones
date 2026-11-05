// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "BaseZoneCommand.h"
#include <QVariantList>

namespace PlasmaZones {

/**
 * @brief Command for pasting multiple zones in one atomic operation
 *
 * Handles paste of multiple zones with proper batch signal deferral.
 * Both undo and redo are atomic operations that defer signals until complete.
 */
class PasteZonesCommand : public BaseZoneCommand
{
public:
    /**
     * @brief Construct a paste zones command
     * @param zoneManager The zone manager
     * @param zonesData List of complete zone data for all pasted zones
     * @param text Command description for undo stack
     * @param parent Parent command (for macro support)
     */
    PasteZonesCommand(QPointer<ZoneManager> zoneManager, const QVariantList& zonesData, const QString& text = QString(),
                      QUndoCommand* parent = nullptr);

    void undo() override;
    void redo() override;

    int id() const override
    {
        return -1;
    } // No merging for paste operations

    /**
     * @brief Get the zone IDs that were pasted
     * @return List of zone IDs
     */
    QStringList pastedZoneIds() const;

private:
    QVariantList m_zonesData;
    QStringList m_zoneIds;
    bool m_firstRedo = true; // Skip first redo since zones are already added
};

} // namespace PlasmaZones
