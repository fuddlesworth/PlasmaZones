// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "BaseZoneCommand.h"
#include "../../services/ZoneManager.h"
#include <QString>

namespace PlasmaZones {

/**
 * @brief Command for filling a zone (expand to fill space)
 *
 * Replays geometry snapshots, so a fixed zone gets its pixels back verbatim
 * rather than a relative rect read as pixels.
 */
class FillZoneCommand : public BaseZoneCommand
{
public:
    explicit FillZoneCommand(QPointer<ZoneManager> zoneManager, const ZoneGeometrySnapshot& oldGeometry,
                             const ZoneGeometrySnapshot& newGeometry, const QString& text,
                             QUndoCommand* parent = nullptr);

    void undo() override;
    void redo() override;
    int id() const override
    {
        return -1;
    } // No merging

private:
    ZoneGeometrySnapshot m_oldGeometry;
    ZoneGeometrySnapshot m_newGeometry;
};

} // namespace PlasmaZones
