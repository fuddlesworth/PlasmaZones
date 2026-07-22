// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "DeleteZoneCommand.h"
#include "../../services/ZoneManager.h"
#include "core/types/constants.h"
#include "core/platform/logging.h"
#include "phosphor_i18n.h"

using namespace PlasmaZones;

DeleteZoneCommand::DeleteZoneCommand(QPointer<ZoneManager> zoneManager, const QString& zoneId,
                                     const QVariantMap& zoneData, const QString& text, QUndoCommand* parent)
    : BaseZoneCommand(zoneManager, text.isEmpty() ? PhosphorI18n::tr("Delete Zone", "@action") : text, parent)
    , m_zoneId(zoneId)
    , m_zoneData(zoneData)
    , m_zoneIndex(zoneManager ? zoneManager->findZoneIndex(zoneId) : -1)
{
}

void DeleteZoneCommand::undo()
{
    if (!m_zoneManager || m_zoneId.isEmpty() || m_zoneData.isEmpty()) {
        return;
    }
    // Restore the zone at the index it was deleted from (allow ID reuse for
    // undo/redo), so it returns to its original height in the stack.
    m_zoneManager->addZoneFromMap(m_zoneData, true, m_zoneIndex);
}

void DeleteZoneCommand::redo()
{
    if (!validateZoneExists(m_zoneId)) {
        return;
    }
    m_zoneManager->deleteZone(m_zoneId);
}
