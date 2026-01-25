// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "UpdateZoneNameCommand.h"
#include "../../services/ZoneManager.h"
#include "../../../core/logging.h"
#include <KLocalizedString>

using namespace PlasmaZones;

UpdateZoneNameCommand::UpdateZoneNameCommand(QPointer<ZoneManager> zoneManager, const QString& zoneId,
                                             const QString& oldName, const QString& newName, const QString& text,
                                             QUndoCommand* parent)
    : BaseZoneCommand(zoneManager, text.isEmpty() ? i18nc("@action", "Rename Zone") : text, parent)
    , m_zoneId(zoneId)
    , m_oldName(oldName)
    , m_newName(newName)
{
}

void UpdateZoneNameCommand::undo()
{
    if (!m_zoneManager || m_zoneId.isEmpty()) {
        return;
    }
    // Validate zone exists before update
    QVariantMap zone = m_zoneManager->getZoneById(m_zoneId);
    if (zone.isEmpty()) {
        qCWarning(lcEditorUndo) << "Zone not found for undo:" << m_zoneId;
        return;
    }
    m_zoneManager->updateZoneName(m_zoneId, m_oldName);
}

void UpdateZoneNameCommand::redo()
{
    if (!m_zoneManager || m_zoneId.isEmpty()) {
        return;
    }
    // Validate zone exists before update
    QVariantMap zone = m_zoneManager->getZoneById(m_zoneId);
    if (zone.isEmpty()) {
        qCWarning(lcEditorUndo) << "Zone not found for redo:" << m_zoneId;
        return;
    }
    m_zoneManager->updateZoneName(m_zoneId, m_newName);
}
