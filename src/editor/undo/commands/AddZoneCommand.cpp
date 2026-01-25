// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "AddZoneCommand.h"
#include "../../services/ZoneManager.h"
#include "../../../core/constants.h"
#include "../../../core/logging.h"
#include <KLocalizedString>

using namespace PlasmaZones;

namespace PlasmaZones {

AddZoneCommand::AddZoneCommand(QPointer<ZoneManager> zoneManager, const QString& zoneId, const QVariantMap& zoneData,
                               const QString& text, QUndoCommand* parent)
    : BaseZoneCommand(zoneManager, text.isEmpty() ? i18nc("@action", "Add Zone") : text, parent)
    , m_zoneId(zoneId)
    , m_zoneData(zoneData)
{
}

void AddZoneCommand::undo()
{
    if (!m_zoneManager || m_zoneId.isEmpty()) {
        return;
    }
    // Validate zone exists before deletion
    QVariantMap zone = m_zoneManager->getZoneById(m_zoneId);
    if (zone.isEmpty()) {
        qCWarning(lcEditorUndo) << "Zone not found for undo:" << m_zoneId;
        return;
    }
    m_zoneManager->deleteZone(m_zoneId);
}

void AddZoneCommand::redo()
{
    if (!m_zoneManager || m_zoneId.isEmpty() || m_zoneData.isEmpty()) {
        return;
    }

    // Check if zone already exists (operation was already performed)
    // QUndoStack automatically calls redo() when pushing, so we need to be idempotent
    QVariantMap existingZone = m_zoneManager->getZoneById(m_zoneId);
    if (!existingZone.isEmpty()) {
        // Zone already exists - this means the operation was already performed
        // QUndoStack calls redo() when pushing, but we've already done the operation
        // So we should do nothing (idempotent) - don't overwrite the zone's current state
        // The zone might have been modified by subsequent operations
        return;
    }

    // Zone doesn't exist - restore it (this happens when redoing after an undo)
    m_zoneManager->addZoneFromMap(m_zoneData, true);
}

} // namespace PlasmaZones
