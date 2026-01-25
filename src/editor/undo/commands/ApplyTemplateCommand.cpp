// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ApplyTemplateCommand.h"
#include "../../services/ZoneManager.h"
#include <KLocalizedString>

using namespace PlasmaZones;

ApplyTemplateCommand::ApplyTemplateCommand(QPointer<ZoneManager> zoneManager, const QString& templateType,
                                           const QVariantList& oldZones, const QVariantList& newZones,
                                           const QString& text, QUndoCommand* parent)
    : BaseZoneCommand(zoneManager, text.isEmpty() ? i18nc("@action", "Apply Template: %1", templateType) : text, parent)
    , m_templateType(templateType)
    , m_oldZones(oldZones)
    , m_newZones(newZones)
{
}

void ApplyTemplateCommand::undo()
{
    if (!m_zoneManager || m_oldZones.isEmpty()) {
        return;
    }
    // Restore old zones
    m_zoneManager->restoreZones(m_oldZones);
}

void ApplyTemplateCommand::redo()
{
    if (!m_zoneManager || m_newZones.isEmpty()) {
        return;
    }
    // Apply new zones (template zones)
    m_zoneManager->restoreZones(m_newZones);
}
