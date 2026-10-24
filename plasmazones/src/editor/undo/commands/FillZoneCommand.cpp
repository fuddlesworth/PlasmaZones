// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "FillZoneCommand.h"
#include "../../services/ZoneManager.h"
#include "core/platform/logging.h"
#include "phosphor_i18n.h"

using namespace PlasmaZones;

FillZoneCommand::FillZoneCommand(QPointer<ZoneManager> zoneManager, const ZoneGeometrySnapshot& oldGeometry,
                                 const ZoneGeometrySnapshot& newGeometry, const QString& text, QUndoCommand* parent)
    : BaseZoneCommand(zoneManager, text.isEmpty() ? PhosphorI18n::tr("Fill Zone", "@action") : text, parent)
    , m_oldGeometry(oldGeometry)
    , m_newGeometry(newGeometry)
{
}

void FillZoneCommand::undo()
{
    if (!validateZoneExists(m_oldGeometry.zoneId)) {
        return;
    }
    m_zoneManager->restoreGeometrySnapshot(m_oldGeometry);
}

void FillZoneCommand::redo()
{
    if (!validateZoneExists(m_newGeometry.zoneId)) {
        return;
    }
    m_zoneManager->restoreGeometrySnapshot(m_newGeometry);
}
