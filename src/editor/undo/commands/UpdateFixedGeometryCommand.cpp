// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "UpdateFixedGeometryCommand.h"
#include "../../services/ZoneManager.h"
#include "../../../core/constants.h"
#include "pz_i18n.h"

using namespace PlasmaZones;

UpdateFixedGeometryCommand::UpdateFixedGeometryCommand(QPointer<ZoneManager> zoneManager, const QString& zoneId,
                                                       const QRectF& oldFixed, const QRectF& newFixed,
                                                       const QRectF& oldRelative, const QRectF& newRelative,
                                                       QUndoCommand* parent)
    : BaseZoneCommand(zoneManager, PzI18n::tr("Change PhosphorZones::Zone Dimensions", "@action"), parent)
    , m_zoneId(zoneId)
    , m_oldFixed(oldFixed)
    , m_newFixed(newFixed)
    , m_oldRelative(oldRelative)
    , m_newRelative(newRelative)
{
}

void UpdateFixedGeometryCommand::applyGeometry(const QRectF& fixed, const QRectF& relative)
{
    if (!m_zoneManager) {
        return;
    }

    QVariantMap zone = m_zoneManager->getZoneById(m_zoneId);
    if (zone.isEmpty()) {
        return;
    }

    zone[::PhosphorZones::ZoneJsonKeys::FixedX] = fixed.x();
    zone[::PhosphorZones::ZoneJsonKeys::FixedY] = fixed.y();
    zone[::PhosphorZones::ZoneJsonKeys::FixedWidth] = fixed.width();
    zone[::PhosphorZones::ZoneJsonKeys::FixedHeight] = fixed.height();
    zone[::PhosphorZones::ZoneJsonKeys::X] = relative.x();
    zone[::PhosphorZones::ZoneJsonKeys::Y] = relative.y();
    zone[::PhosphorZones::ZoneJsonKeys::Width] = relative.width();
    zone[::PhosphorZones::ZoneJsonKeys::Height] = relative.height();

    m_zoneManager->setZoneData(m_zoneId, zone);
}

void UpdateFixedGeometryCommand::undo()
{
    if (!validateZoneExists(m_zoneId)) {
        return;
    }
    m_zoneManager->beginBatchUpdate();
    applyGeometry(m_oldFixed, m_oldRelative);
    m_zoneManager->endBatchUpdate();
}

void UpdateFixedGeometryCommand::redo()
{
    if (!validateZoneExists(m_zoneId)) {
        return;
    }
    m_zoneManager->beginBatchUpdate();
    applyGeometry(m_newFixed, m_newRelative);
    m_zoneManager->endBatchUpdate();
}

bool UpdateFixedGeometryCommand::mergeWith(const QUndoCommand* other)
{
    if (other->id() != id()) {
        return false;
    }

    const auto* cmd = static_cast<const UpdateFixedGeometryCommand*>(other);
    if (cmd->m_zoneId != m_zoneId) {
        return false;
    }

    // Merge: keep old state, update new state, apply immediately
    m_newFixed = cmd->m_newFixed;
    m_newRelative = cmd->m_newRelative;
    applyGeometry(m_newFixed, m_newRelative);
    return true;
}
