// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "UpdateZoneGeometryCommand.h"
#include "../../services/ZoneManager.h"
#include "../../../core/logging.h"
#include <KLocalizedString>

using namespace PlasmaZones;

UpdateZoneGeometryCommand::UpdateZoneGeometryCommand(QPointer<ZoneManager> zoneManager, const QString& zoneId,
                                                     const QRectF& oldGeometry, const QRectF& newGeometry,
                                                     const QString& text, QUndoCommand* parent)
    : BaseZoneCommand(zoneManager, text.isEmpty() ? i18nc("@action", "Move Zone") : text, parent)
    , m_zoneId(zoneId)
    , m_oldGeometry(oldGeometry)
    , m_newGeometry(newGeometry)
{
}

void UpdateZoneGeometryCommand::undo()
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
    // Use batch so emissions happen in endBatchUpdate, after the undo stack has
    // finished its internal bookkeeping; reduces "invalid context" during undo.
    m_zoneManager->beginBatchUpdate();
    m_zoneManager->updateZoneGeometry(m_zoneId, m_oldGeometry.x(), m_oldGeometry.y(), m_oldGeometry.width(),
                                      m_oldGeometry.height());
    m_zoneManager->endBatchUpdate();
}

void UpdateZoneGeometryCommand::redo()
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
    // Use batch for consistency with undo() and with DividerResizeCommand, BatchUpdateAppearanceCommand, etc.
    m_zoneManager->beginBatchUpdate();
    m_zoneManager->updateZoneGeometry(m_zoneId, m_newGeometry.x(), m_newGeometry.y(), m_newGeometry.width(),
                                      m_newGeometry.height());
    m_zoneManager->endBatchUpdate();
}

bool UpdateZoneGeometryCommand::mergeWith(const QUndoCommand* other)
{
    if (other->id() != id()) {
        return false;
    }

    const auto* cmd = static_cast<const UpdateZoneGeometryCommand*>(other);
    if (cmd->m_zoneId != m_zoneId) {
        return false; // Different zones, don't merge
    }

    // Merge: keep old geometry, update new geometry.
    // QUndoStack does not call redo() on the merged command, so we must apply
    // the new geometry to the zone so the model matches the merged state.
    UpdateZoneGeometryCommand* self = const_cast<UpdateZoneGeometryCommand*>(this);
    self->m_newGeometry = cmd->m_newGeometry;
    if (m_zoneManager && !m_zoneId.isEmpty()) {
        m_zoneManager->updateZoneGeometry(m_zoneId, m_newGeometry.x(), m_newGeometry.y(), m_newGeometry.width(),
                                          m_newGeometry.height());
    }
    return true;
}
