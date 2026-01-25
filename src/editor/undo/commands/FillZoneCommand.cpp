// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "FillZoneCommand.h"
#include "../../services/ZoneManager.h"
#include "../../../core/logging.h"
#include <KLocalizedString>

using namespace PlasmaZones;

FillZoneCommand::FillZoneCommand(QPointer<ZoneManager> zoneManager, const QString& zoneId, const QRectF& oldGeometry,
                                 const QRectF& newGeometry, const QString& text, QUndoCommand* parent)
    : BaseZoneCommand(zoneManager, text.isEmpty() ? i18nc("@action", "Fill Zone") : text, parent)
    , m_zoneId(zoneId)
    , m_oldGeometry(oldGeometry)
    , m_newGeometry(newGeometry)
{
}

void FillZoneCommand::undo()
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
    m_zoneManager->updateZoneGeometry(m_zoneId, m_oldGeometry.x(), m_oldGeometry.y(), m_oldGeometry.width(),
                                      m_oldGeometry.height());
}

void FillZoneCommand::redo()
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
    m_zoneManager->updateZoneGeometry(m_zoneId, m_newGeometry.x(), m_newGeometry.y(), m_newGeometry.width(),
                                      m_newGeometry.height());
}
