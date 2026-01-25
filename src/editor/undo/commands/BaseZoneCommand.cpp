// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "BaseZoneCommand.h"
#include "../../services/ZoneManager.h"

namespace PlasmaZones {

BaseZoneCommand::BaseZoneCommand(QPointer<ZoneManager> zoneManager, const QString& text, QUndoCommand* parent)
    : QUndoCommand(text, parent)
    , m_zoneManager(zoneManager)
{
}

} // namespace PlasmaZones
