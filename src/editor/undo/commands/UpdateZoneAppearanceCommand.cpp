// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "UpdateZoneAppearanceCommand.h"
#include "../../services/ZoneManager.h"
#include "../../../core/logging.h"
#include <KLocalizedString>

using namespace PlasmaZones;

UpdateZoneAppearanceCommand::UpdateZoneAppearanceCommand(QPointer<ZoneManager> zoneManager, const QString& zoneId,
                                                         const QString& propertyName, const QVariant& oldValue,
                                                         const QVariant& newValue, const QString& text,
                                                         QUndoCommand* parent)
    : BaseZoneCommand(zoneManager, text.isEmpty() ? i18nc("@action", "Change Zone Appearance") : text, parent)
    , m_zoneId(zoneId)
    , m_propertyName(propertyName)
    , m_oldValue(oldValue)
    , m_newValue(newValue)
{
}

void UpdateZoneAppearanceCommand::undo()
{
    if (!m_zoneManager || m_zoneId.isEmpty() || m_propertyName.isEmpty()) {
        return;
    }
    // Validate zone exists before update
    QVariantMap zone = m_zoneManager->getZoneById(m_zoneId);
    if (zone.isEmpty()) {
        qCWarning(lcEditorUndo) << "Zone not found for undo:" << m_zoneId;
        return;
    }
    m_zoneManager->updateZoneAppearance(m_zoneId, m_propertyName, m_oldValue);
}

void UpdateZoneAppearanceCommand::redo()
{
    if (!m_zoneManager || m_zoneId.isEmpty() || m_propertyName.isEmpty()) {
        return;
    }
    // Validate zone exists before update
    QVariantMap zone = m_zoneManager->getZoneById(m_zoneId);
    if (zone.isEmpty()) {
        qCWarning(lcEditorUndo) << "Zone not found for redo:" << m_zoneId;
        return;
    }
    m_zoneManager->updateZoneAppearance(m_zoneId, m_propertyName, m_newValue);
}

bool UpdateZoneAppearanceCommand::mergeWith(const QUndoCommand* other)
{
    if (other->id() != id()) {
        return false;
    }

    const auto* cmd = static_cast<const UpdateZoneAppearanceCommand*>(other);
    if (cmd->m_zoneId != m_zoneId || cmd->m_propertyName != m_propertyName) {
        return false; // Different zones or properties, don't merge
    }

    // Merge: keep old value, update new value
    const_cast<UpdateZoneAppearanceCommand*>(this)->m_newValue = cmd->m_newValue;
    return true;
}
