// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "UpdateZoneNumberCommand.h"
#include "../../services/ZoneManager.h"
#include "../../../core/logging.h"
#include <KLocalizedString>

using namespace PlasmaZones;

namespace PlasmaZones {

UpdateZoneNumberCommand::UpdateZoneNumberCommand(QPointer<ZoneManager> zoneManager, const QString& zoneId,
                                                 int oldNumber, int newNumber, const QString& text,
                                                 QUndoCommand* parent)
    : BaseZoneCommand(zoneManager, text.isEmpty() ? i18nc("@action", "Update Zone Number") : text, parent)
    , m_zoneId(zoneId)
    , m_oldNumber(oldNumber)
    , m_newNumber(newNumber)
{
}

void UpdateZoneNumberCommand::undo()
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
    m_zoneManager->updateZoneNumber(m_zoneId, m_oldNumber);
}

void UpdateZoneNumberCommand::redo()
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
    m_zoneManager->updateZoneNumber(m_zoneId, m_newNumber);
}

} // namespace PlasmaZones
