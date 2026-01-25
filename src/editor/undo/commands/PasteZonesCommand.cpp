// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "PasteZonesCommand.h"
#include "../../services/ZoneManager.h"
#include "../../../core/constants.h"
#include <KLocalizedString>

using namespace PlasmaZones;

PasteZonesCommand::PasteZonesCommand(QPointer<ZoneManager> zoneManager, const QVariantList& zonesData,
                                     const QString& text, QUndoCommand* parent)
    : BaseZoneCommand(zoneManager, text.isEmpty() ? i18nc("@action", "Paste %1 Zones", zonesData.count()) : text,
                      parent)
    , m_zonesData(zonesData)
{
    // Extract zone IDs from the data
    for (const QVariant& zoneVar : zonesData) {
        QVariantMap zone = zoneVar.toMap();
        QString zoneId = zone.value(QString::fromLatin1(JsonKeys::Id)).toString();
        if (!zoneId.isEmpty()) {
            m_zoneIds.append(zoneId);
        }
    }
}

void PasteZonesCommand::undo()
{
    if (!m_zoneManager || m_zoneIds.isEmpty()) {
        return;
    }

    // Use batch update to defer signals until all zones are deleted
    m_zoneManager->beginBatchUpdate();

    for (const QString& zoneId : m_zoneIds) {
        m_zoneManager->deleteZone(zoneId);
    }

    m_zoneManager->endBatchUpdate();
}

void PasteZonesCommand::redo()
{
    if (!m_zoneManager || m_zonesData.isEmpty()) {
        return;
    }

    // Skip first redo since zones are already added by pasteZones()
    if (m_firstRedo) {
        m_firstRedo = false;
        return;
    }

    // Use batch update to defer signals until all zones are added
    m_zoneManager->beginBatchUpdate();

    for (const QVariant& zoneVar : m_zonesData) {
        QVariantMap zone = zoneVar.toMap();
        m_zoneManager->addZoneFromMap(zone, true); // allowIdReuse for redo
    }

    m_zoneManager->endBatchUpdate();
}

QStringList PasteZonesCommand::pastedZoneIds() const
{
    return m_zoneIds;
}
