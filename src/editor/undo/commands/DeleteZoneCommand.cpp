// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "DeleteZoneCommand.h"
#include "../../services/ZoneManager.h"
#include "../../../core/constants.h"
#include "../../../core/logging.h"
#include "pz_i18n.h"

using namespace PlasmaZones;

DeleteZoneCommand::DeleteZoneCommand(QPointer<ZoneManager> zoneManager, const QString& zoneId,
                                     const QVariantMap& zoneData, const QString& text, QUndoCommand* parent)
    : BaseZoneCommand(zoneManager, text.isEmpty() ? PzI18n::tr("Delete Zone", "@action") : text, parent)
    , m_zoneId(zoneId)
    , m_zoneData(zoneData)
{
}

void DeleteZoneCommand::undo()
{
    if (!m_zoneManager || m_zoneId.isEmpty() || m_zoneData.isEmpty()) {
        return;
    }
    // Restore zone with original data (allow ID reuse for undo/redo)
    m_zoneManager->addZoneFromMap(m_zoneData, true);
}

void DeleteZoneCommand::redo()
{
    if (!validateZoneExists(m_zoneId)) {
        return;
    }
    m_zoneManager->deleteZone(m_zoneId);
}
