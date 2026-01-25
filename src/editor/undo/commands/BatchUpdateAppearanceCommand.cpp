// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "BatchUpdateAppearanceCommand.h"
#include "../../services/ZoneManager.h"
#include <KLocalizedString>

using namespace PlasmaZones;

// ═══════════════════════════════════════════════════════════════════════════════
// BatchUpdateAppearanceCommand
// ═══════════════════════════════════════════════════════════════════════════════

BatchUpdateAppearanceCommand::BatchUpdateAppearanceCommand(QPointer<ZoneManager> zoneManager,
                                                           const QStringList& zoneIds, const QString& propertyName,
                                                           const QMap<QString, QVariant>& oldValues,
                                                           const QVariant& newValue, const QString& text,
                                                           QUndoCommand* parent)
    : BaseZoneCommand(zoneManager,
                      text.isEmpty() ? i18nc("@action", "Update Appearance for %1 Zones", zoneIds.count()) : text,
                      parent)
    , m_zoneIds(zoneIds)
    , m_propertyName(propertyName)
    , m_oldValues(oldValues)
    , m_newValue(newValue)
{
}

void BatchUpdateAppearanceCommand::undo()
{
    if (!m_zoneManager || m_zoneIds.isEmpty() || m_propertyName.isEmpty()) {
        return;
    }

    // Use batch update to defer signals until all zones are updated
    m_zoneManager->beginBatchUpdate();

    for (const QString& zoneId : m_zoneIds) {
        if (m_oldValues.contains(zoneId)) {
            m_zoneManager->updateZoneAppearance(zoneId, m_propertyName, m_oldValues.value(zoneId));
        }
    }

    m_zoneManager->endBatchUpdate();
}

void BatchUpdateAppearanceCommand::redo()
{
    if (!m_zoneManager || m_zoneIds.isEmpty() || m_propertyName.isEmpty()) {
        return;
    }

    // Use batch update to defer signals until all zones are updated
    m_zoneManager->beginBatchUpdate();

    for (const QString& zoneId : m_zoneIds) {
        m_zoneManager->updateZoneAppearance(zoneId, m_propertyName, m_newValue);
    }

    m_zoneManager->endBatchUpdate();
}

// ═══════════════════════════════════════════════════════════════════════════════
// BatchUpdateColorCommand
// ═══════════════════════════════════════════════════════════════════════════════

BatchUpdateColorCommand::BatchUpdateColorCommand(QPointer<ZoneManager> zoneManager, const QStringList& zoneIds,
                                                 const QString& colorType, const QMap<QString, QString>& oldColors,
                                                 const QString& newColor, const QString& text, QUndoCommand* parent)
    : BaseZoneCommand(zoneManager,
                      text.isEmpty() ? i18nc("@action", "Update Color for %1 Zones", zoneIds.count()) : text, parent)
    , m_zoneIds(zoneIds)
    , m_colorType(colorType)
    , m_oldColors(oldColors)
    , m_newColor(newColor)
{
}

void BatchUpdateColorCommand::undo()
{
    if (!m_zoneManager || m_zoneIds.isEmpty() || m_colorType.isEmpty()) {
        return;
    }

    // Use batch update to defer signals until all zones are updated
    m_zoneManager->beginBatchUpdate();

    for (const QString& zoneId : m_zoneIds) {
        if (m_oldColors.contains(zoneId)) {
            m_zoneManager->updateZoneColor(zoneId, m_colorType, m_oldColors.value(zoneId));
        }
    }

    m_zoneManager->endBatchUpdate();
}

void BatchUpdateColorCommand::redo()
{
    if (!m_zoneManager || m_zoneIds.isEmpty() || m_colorType.isEmpty()) {
        return;
    }

    // Use batch update to defer signals until all zones are updated
    m_zoneManager->beginBatchUpdate();

    for (const QString& zoneId : m_zoneIds) {
        m_zoneManager->updateZoneColor(zoneId, m_colorType, m_newColor);
    }

    m_zoneManager->endBatchUpdate();
}
