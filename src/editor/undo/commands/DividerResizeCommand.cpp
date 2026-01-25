// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "DividerResizeCommand.h"
#include "../../services/ZoneManager.h"
#include "../../../core/logging.h"
#include <KLocalizedString>

using namespace PlasmaZones;

DividerResizeCommand::DividerResizeCommand(QPointer<ZoneManager> zoneManager, const QString& zoneId1,
                                           const QString& zoneId2, qreal newDividerX, qreal newDividerY,
                                           bool isVertical, GeometryMap&& oldGeometries, const QString& text,
                                           QUndoCommand* parent)
    : BaseZoneCommand(zoneManager, text.isEmpty() ? i18nc("@action", "Resize at Divider") : text, parent)
    , m_zoneId1(zoneId1)
    , m_zoneId2(zoneId2)
    , m_newDividerX(newDividerX)
    , m_newDividerY(newDividerY)
    , m_isVertical(isVertical)
    , m_oldGeometries(std::move(oldGeometries))
{
}

void DividerResizeCommand::undo()
{
    if (!m_zoneManager) {
        return;
    }
    // Use batch update to defer signals until all zones are restored (matches
    // BatchUpdateAppearanceCommand, BatchUpdateColorCommand, PasteZonesCommand).
    m_zoneManager->beginBatchUpdate();
    for (const auto& p : m_oldGeometries) {
        const QString& zoneId = p.first;
        const QRectF& rect = p.second;
        if (zoneId.isEmpty()) {
            continue;
        }
        m_zoneManager->updateZoneGeometry(zoneId, rect.x(), rect.y(), rect.width(), rect.height());
    }
    m_zoneManager->endBatchUpdate();
}

void DividerResizeCommand::redo()
{
    if (!m_zoneManager || m_zoneId1.isEmpty() || m_zoneId2.isEmpty()) {
        return;
    }
    // Use batch update to defer signals until all zones are resized (matches
    // BatchUpdateAppearanceCommand, BatchUpdateColorCommand, PasteZonesCommand).
    m_zoneManager->beginBatchUpdate();
    m_zoneManager->resizeZonesAtDivider(m_zoneId1, m_zoneId2, m_newDividerX, m_newDividerY, m_isVertical);
    m_zoneManager->endBatchUpdate();
}
