// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "BaseZoneCommand.h"
#include "../../services/ZoneManager.h"
#include "../../../core/logging.h"

namespace PlasmaZones {

BaseZoneCommand::BaseZoneCommand(QPointer<ZoneManager> zoneManager, const QString& text, QUndoCommand* parent)
    : QUndoCommand(text, parent)
    , m_zoneManager(zoneManager)
{
}

bool BaseZoneCommand::validateZoneExists(const QString& zoneId) const
{
    if (!m_zoneManager || zoneId.isEmpty()) {
        return false;
    }
    QVariantMap zone = m_zoneManager->getZoneById(zoneId);
    if (zone.isEmpty()) {
        qCWarning(lcEditorUndo) << "Zone not found:" << zoneId;
        return false;
    }
    return true;
}

QVariantMap BaseZoneCommand::getValidatedZone(const QString& zoneId) const
{
    if (!m_zoneManager || zoneId.isEmpty()) {
        return QVariantMap();
    }
    QVariantMap zone = m_zoneManager->getZoneById(zoneId);
    if (zone.isEmpty()) {
        qCWarning(lcEditorUndo) << "Zone not found:" << zoneId;
    }
    return zone;
}

} // namespace PlasmaZones
