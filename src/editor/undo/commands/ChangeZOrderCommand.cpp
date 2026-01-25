// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ChangeZOrderCommand.h"
#include "../../services/ZoneManager.h"
#include <KLocalizedString>

using namespace PlasmaZones;

ChangeZOrderCommand::ChangeZOrderCommand(QPointer<ZoneManager> zoneManager, const QString& zoneId,
                                         const QVariantList& oldZones, const QVariantList& newZones,
                                         const QString& text, QUndoCommand* parent)
    : BaseZoneCommand(zoneManager, text.isEmpty() ? i18nc("@action", "Change Z-Order") : text, parent)
    , m_zoneId(zoneId)
    , m_oldZones(oldZones)
    , m_newZones(newZones)
{
}

void ChangeZOrderCommand::undo()
{
    if (!m_zoneManager || m_oldZones.isEmpty()) {
        return;
    }
    m_zoneManager->restoreZones(m_oldZones);
}

void ChangeZOrderCommand::redo()
{
    if (!m_zoneManager || m_newZones.isEmpty()) {
        return;
    }
    m_zoneManager->restoreZones(m_newZones);
}

bool ChangeZOrderCommand::mergeWith(const QUndoCommand* other)
{
    if (other->id() != id()) {
        return false;
    }

    const auto* cmd = static_cast<const ChangeZOrderCommand*>(other);
    if (cmd->m_zoneId != m_zoneId) {
        return false; // Different zones, don't merge
    }

    // Merge: keep old zones, update new zones
    const_cast<ChangeZOrderCommand*>(this)->m_newZones = cmd->m_newZones;
    return true;
}
