// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "DeleteZoneWithFillCommand.h"
#include "../../services/ZoneManager.h"
#include <KLocalizedString>

using namespace PlasmaZones;

DeleteZoneWithFillCommand::DeleteZoneWithFillCommand(QPointer<ZoneManager> zoneManager, const QString& zoneId,
                                                     const QVariantMap& deletedZoneData, const QVariantList& oldZones,
                                                     const QVariantList& newZones, const QString& text,
                                                     QUndoCommand* parent)
    : BaseZoneCommand(zoneManager, text.isEmpty() ? i18nc("@action", "Delete Zone") : text, parent)
    , m_zoneId(zoneId)
    , m_deletedZoneData(deletedZoneData)
    , m_oldZones(oldZones)
    , m_newZones(newZones)
{
}

void DeleteZoneWithFillCommand::undo()
{
    if (!m_zoneManager || m_oldZones.isEmpty()) {
        return;
    }
    // Restore complete zones list (includes deleted zone and original affected zones)
    m_zoneManager->restoreZones(m_oldZones);
}

void DeleteZoneWithFillCommand::redo()
{
    if (!m_zoneManager || m_newZones.isEmpty()) {
        return;
    }
    // Restore zones list after deletion and fill
    m_zoneManager->restoreZones(m_newZones);
}
