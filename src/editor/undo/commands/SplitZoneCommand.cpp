// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "SplitZoneCommand.h"
#include "../../services/ZoneManager.h"
#include "../../../core/constants.h"
#include "../../../core/logging.h"
#include <KLocalizedString>
#include <QSet>

using namespace PlasmaZones;

SplitZoneCommand::SplitZoneCommand(QPointer<ZoneManager> zoneManager, const QString& originalZoneId,
                                   const QVariantMap& originalZoneData, const QVariantList& newZonesData,
                                   const QString& text, QUndoCommand* parent)
    : BaseZoneCommand(zoneManager, text.isEmpty() ? i18nc("@action", "Split Zone") : text, parent)
    , m_originalZoneId(originalZoneId)
    , m_originalZoneData(originalZoneData)
    , m_newZonesData(newZonesData)
{
}

void SplitZoneCommand::undo()
{
    if (!m_zoneManager || m_originalZoneId.isEmpty() || m_originalZoneData.isEmpty()) {
        return;
    }

    // Validate original zone data
    if (!m_originalZoneData.contains(JsonKeys::Id) || !m_originalZoneData.contains(JsonKeys::X)
        || !m_originalZoneData.contains(JsonKeys::Y) || !m_originalZoneData.contains(JsonKeys::Width)
        || !m_originalZoneData.contains(JsonKeys::Height)) {
        qCWarning(lcEditorUndo) << "Original zone data is invalid, missing required fields";
        return;
    }

    // Ensure original zone ID is correct
    QVariantMap originalZone = m_originalZoneData;
    if (originalZone[JsonKeys::Id].toString() != m_originalZoneId) {
        originalZone[JsonKeys::Id] = m_originalZoneId;
    }

    // Get current zones list
    QVariantList currentZones = m_zoneManager->zones();

    // Build new zones list: remove all zones from newZonesData, then add original zone
    QVariantList newZonesList;
    QSet<QString> newZoneIds;

    // Collect IDs of zones to remove (all zones from newZonesData)
    // Note: This includes the modified original zone (same ID, different geometry)
    for (const QVariant& zoneVar : m_newZonesData) {
        if (!zoneVar.canConvert<QVariantMap>())
            continue;
        QVariantMap zone = zoneVar.toMap();
        QString zoneId = zone[JsonKeys::Id].toString();
        if (!zoneId.isEmpty()) {
            newZoneIds.insert(zoneId);
        }
    }

    // Add all existing zones except the ones that were created/modified by the split
    for (const QVariant& zoneVar : currentZones) {
        QVariantMap zone = zoneVar.toMap();
        QString zoneId = zone[JsonKeys::Id].toString();
        if (!newZoneIds.contains(zoneId)) {
            newZonesList.append(zone);
        }
    }

    // Always add the original zone (restore it to its pre-split state)
    // We've already excluded the modified original zone above (it has the same ID but different geometry),
    // so we can safely add the original zone back with its original geometry
    newZonesList.append(originalZone);

    // Use restoreZones() to atomically replace the entire zones list
    // This prevents QML from evaluating while the list is in an inconsistent state
    m_zoneManager->restoreZones(newZonesList);
}

void SplitZoneCommand::redo()
{
    if (!m_zoneManager || m_originalZoneId.isEmpty() || m_newZonesData.isEmpty()) {
        return;
    }

    // Get current zones list
    QVariantList currentZones = m_zoneManager->zones();

    // Build new zones list: keep all zones except the original, then add new zones
    QVariantList newZonesList;

    // First, add all existing zones except the original
    for (const QVariant& zoneVar : currentZones) {
        QVariantMap zone = zoneVar.toMap();
        QString zoneId = zone[JsonKeys::Id].toString();
        if (zoneId != m_originalZoneId) {
            newZonesList.append(zone);
        }
    }

    // Then add all new zones (which includes the modified original zone)
    for (const QVariant& zoneVar : m_newZonesData) {
        if (!zoneVar.canConvert<QVariantMap>()) {
            qCWarning(lcEditorUndo) << "Invalid zone data in redo";
            continue;
        }
        newZonesList.append(zoneVar);
    }

    // Use restoreZones() to atomically replace the entire zones list
    // This prevents QML from evaluating while the list is in an inconsistent state
    m_zoneManager->restoreZones(newZonesList);
}
