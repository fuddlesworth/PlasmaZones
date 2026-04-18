// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "UpdateZoneNumberCommand.h"
#include "../../services/ZoneManager.h"
#include "../../../core/logging.h"
#include "pz_i18n.h"

using namespace PlasmaZones;

namespace PlasmaZones {

UpdateZoneNumberCommand::UpdateZoneNumberCommand(QPointer<ZoneManager> zoneManager, const QString& zoneId,
                                                 int oldNumber, int newNumber, const QString& text,
                                                 QUndoCommand* parent)
    : BaseZoneCommand(zoneManager, text.isEmpty() ? PzI18n::tr("Change PhosphorZones::Zone Number", "@action") : text,
                      parent)
    , m_zoneId(zoneId)
    , m_oldNumber(oldNumber)
    , m_newNumber(newNumber)
{
}

void UpdateZoneNumberCommand::undo()
{
    if (!validateZoneExists(m_zoneId)) {
        return;
    }
    m_zoneManager->updateZoneNumber(m_zoneId, m_oldNumber);
}

void UpdateZoneNumberCommand::redo()
{
    if (!validateZoneExists(m_zoneId)) {
        return;
    }
    m_zoneManager->updateZoneNumber(m_zoneId, m_newNumber);
}

} // namespace PlasmaZones
