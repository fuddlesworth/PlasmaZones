// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ClearAllZonesCommand.h"
#include "../../services/ZoneManager.h"
#include <KLocalizedString>

using namespace PlasmaZones;

ClearAllZonesCommand::ClearAllZonesCommand(QPointer<ZoneManager> zoneManager, const QVariantList& oldZones,
                                           const QString& text, QUndoCommand* parent)
    : BaseZoneCommand(zoneManager, text.isEmpty() ? i18nc("@action", "Clear All Zones") : text, parent)
    , m_oldZones(oldZones)
{
}

void ClearAllZonesCommand::undo()
{
    if (!m_zoneManager) {
        return;
    }
    // Restore all zones
    m_zoneManager->restoreZones(m_oldZones);
}

void ClearAllZonesCommand::redo()
{
    if (!m_zoneManager) {
        return;
    }
    // Clear all zones
    m_zoneManager->clearAllZones();
}
