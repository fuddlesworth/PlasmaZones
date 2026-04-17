// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "UpdateZoneNameCommand.h"
#include "../../services/ZoneManager.h"
#include "../../../core/logging.h"
#include "pz_i18n.h"

using namespace PlasmaZones;

UpdateZoneNameCommand::UpdateZoneNameCommand(QPointer<ZoneManager> zoneManager, const QString& zoneId,
                                             const QString& oldName, const QString& newName, const QString& text,
                                             QUndoCommand* parent)
    : BaseZoneCommand(zoneManager, text.isEmpty() ? PzI18n::tr("Rename PhosphorZones::Zone", "@action") : text, parent)
    , m_zoneId(zoneId)
    , m_oldName(oldName)
    , m_newName(newName)
{
}

void UpdateZoneNameCommand::undo()
{
    if (!validateZoneExists(m_zoneId)) {
        return;
    }
    m_zoneManager->updateZoneName(m_zoneId, m_oldName);
}

void UpdateZoneNameCommand::redo()
{
    if (!validateZoneExists(m_zoneId)) {
        return;
    }
    m_zoneManager->updateZoneName(m_zoneId, m_newName);
}
