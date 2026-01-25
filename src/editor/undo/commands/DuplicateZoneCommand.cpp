// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "DuplicateZoneCommand.h"
#include "../../services/ZoneManager.h"
#include "../../../core/constants.h"
#include "../../../core/logging.h"
#include <KLocalizedString>

using namespace PlasmaZones;

DuplicateZoneCommand::DuplicateZoneCommand(QPointer<ZoneManager> zoneManager, const QString& sourceZoneId,
                                           const QString& duplicatedZoneId, const QVariantMap& duplicatedZoneData,
                                           const QString& text, QUndoCommand* parent)
    : BaseZoneCommand(zoneManager, text.isEmpty() ? i18nc("@action", "Duplicate Zone") : text, parent)
    , m_sourceZoneId(sourceZoneId)
    , m_duplicatedZoneId(duplicatedZoneId)
    , m_duplicatedZoneData(duplicatedZoneData)
{
}

void DuplicateZoneCommand::undo()
{
    if (!m_zoneManager || m_duplicatedZoneId.isEmpty()) {
        return;
    }
    // Validate zone exists before deletion
    QVariantMap zone = m_zoneManager->getZoneById(m_duplicatedZoneId);
    if (zone.isEmpty()) {
        qCWarning(lcEditorUndo) << "Zone not found for undo:" << m_duplicatedZoneId;
        return;
    }
    m_zoneManager->deleteZone(m_duplicatedZoneId);
}

void DuplicateZoneCommand::redo()
{
    if (!m_zoneManager || m_duplicatedZoneId.isEmpty() || m_duplicatedZoneData.isEmpty()) {
        return;
    }

    // Check if zone already exists (operation was already performed)
    // QUndoStack automatically calls redo() when pushing, so we need to be idempotent
    QVariantMap existingZone = m_zoneManager->getZoneById(m_duplicatedZoneId);
    if (!existingZone.isEmpty()) {
        // Zone already exists - this means the operation was already performed
        // QUndoStack calls redo() when pushing, but we've already done the operation
        // So we should do nothing (idempotent) - don't overwrite the zone's current state
        // The zone might have been modified by subsequent operations (e.g., move)
        return;
    }

    // Zone doesn't exist - restore it (this happens when redoing after an undo)
    m_zoneManager->addZoneFromMap(m_duplicatedZoneData, true);
}
