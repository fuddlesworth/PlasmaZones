// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "../ZoneManager.h"
#include "../../../core/constants.h"
#include "../../../core/logging.h"

#include <QUuid>
#include <QSet>

using namespace PlasmaZones;

void ZoneManager::setDefaultColors(const QString& highlightColor, const QString& inactiveColor,
                                   const QString& borderColor)
{
    m_defaultHighlightColor = highlightColor;
    m_defaultInactiveColor = inactiveColor;
    m_defaultBorderColor = borderColor;
}

QString ZoneManager::addZoneFromMap(const QVariantMap& zoneData, bool allowIdReuse)
{
    if (zoneData.isEmpty()) {
        qCWarning(lcEditorZone) << "Empty zone data for addZoneFromMap";
        return QString();
    }

    // Validate required fields
    if (!zoneData.contains(JsonKeys::Id) || !zoneData.contains(JsonKeys::X) || !zoneData.contains(JsonKeys::Y)
        || !zoneData.contains(JsonKeys::Width) || !zoneData.contains(JsonKeys::Height)) {
        qCWarning(lcEditorZone) << "Zone data: invalid, missing required fields";
        return QString();
    }

    // Validate geometry based on geometry mode
    qreal x = zoneData[JsonKeys::X].toDouble();
    qreal y = zoneData[JsonKeys::Y].toDouble();
    qreal width = zoneData[JsonKeys::Width].toDouble();
    qreal height = zoneData[JsonKeys::Height].toDouble();

    if (isFixedMode(zoneData)) {
        // Fixed mode: validate fixed pixel coords
        QRectF fixedGeo = extractFixedGeometry(zoneData);
        if (fixedGeo.x() < 0.0 || fixedGeo.y() < 0.0
            || fixedGeo.width() < static_cast<qreal>(EditorConstants::MinFixedZoneSize)
            || fixedGeo.height() < static_cast<qreal>(EditorConstants::MinFixedZoneSize)) {
            qCWarning(lcEditorZone) << "Invalid fixed zone geometry for addZoneFromMap:" << fixedGeo;
            return QString();
        }
    } else {
        // Relative mode: validate 0-1 range
        if (x < 0.0 || x > 1.0 || y < 0.0 || y > 1.0 || width <= 0.0 || width > 1.0 || height <= 0.0 || height > 1.0) {
            qCWarning(lcEditorZone) << "Invalid zone geometry for addZoneFromMap";
            return QString();
        }
    }

    // Use provided ID or generate new one
    QString zoneId = zoneData[JsonKeys::Id].toString();
    int existingIndex = -1;
    if (zoneId.isEmpty()) {
        // ID is empty, generate new one
        zoneId = QUuid::createUuid().toString();
    } else {
        existingIndex = findZoneIndex(zoneId);
        if (existingIndex >= 0) {
            // ID already exists
            if (allowIdReuse) {
                // For undo/redo: update zone in place instead of deleting/re-adding
                // This prevents QML from trying to access a non-existent zone
                // Don't do anything here - we'll update it below
            } else {
                // For paste operations: generate new ID
                zoneId = QUuid::createUuid().toString();
                existingIndex = -1; // Reset since we're using a new ID
            }
        }
    }

    // Create zone with all properties from zoneData
    QString name = zoneData.contains(JsonKeys::Name) ? zoneData[JsonKeys::Name].toString() : QString();
    int zoneNumber =
        zoneData.contains(JsonKeys::ZoneNumber) ? zoneData[JsonKeys::ZoneNumber].toInt() : m_zones.size() + 1;

    QVariantMap zone = createZone(name, zoneNumber, x, y, width, height);

    // Update ID (createZone generates new ID, but we want to preserve paste ID)
    zone[JsonKeys::Id] = zoneId;

    // Copy all appearance properties
    if (zoneData.contains(JsonKeys::HighlightColor)) {
        zone[JsonKeys::HighlightColor] = zoneData[JsonKeys::HighlightColor].toString();
    }
    if (zoneData.contains(JsonKeys::InactiveColor)) {
        zone[JsonKeys::InactiveColor] = zoneData[JsonKeys::InactiveColor].toString();
    }
    if (zoneData.contains(JsonKeys::BorderColor)) {
        zone[JsonKeys::BorderColor] = zoneData[JsonKeys::BorderColor].toString();
    }
    if (zoneData.contains(JsonKeys::ActiveOpacity)) {
        zone[JsonKeys::ActiveOpacity] = zoneData[JsonKeys::ActiveOpacity].toDouble();
    }
    if (zoneData.contains(JsonKeys::InactiveOpacity)) {
        zone[JsonKeys::InactiveOpacity] = zoneData[JsonKeys::InactiveOpacity].toDouble();
    }
    if (zoneData.contains(JsonKeys::BorderWidth)) {
        zone[JsonKeys::BorderWidth] = zoneData[JsonKeys::BorderWidth].toInt();
    }
    if (zoneData.contains(JsonKeys::BorderRadius)) {
        zone[JsonKeys::BorderRadius] = zoneData[JsonKeys::BorderRadius].toInt();
    }

    QString useCustomColorsKey = QString::fromLatin1(JsonKeys::UseCustomColors);
    if (zoneData.contains(useCustomColorsKey)) {
        zone[useCustomColorsKey] = zoneData[useCustomColorsKey].toBool();
    }

    // Copy geometry mode and fixed geometry
    if (zoneData.contains(JsonKeys::GeometryMode)) {
        zone[JsonKeys::GeometryMode] = zoneData[JsonKeys::GeometryMode].toInt();
    }
    if (zoneData.contains(JsonKeys::FixedX)) {
        zone[JsonKeys::FixedX] = zoneData[JsonKeys::FixedX].toDouble();
        zone[JsonKeys::FixedY] = zoneData[JsonKeys::FixedY].toDouble();
        zone[JsonKeys::FixedWidth] = zoneData[JsonKeys::FixedWidth].toDouble();
        zone[JsonKeys::FixedHeight] = zoneData[JsonKeys::FixedHeight].toDouble();
    }
    // For fixed zones, ensure relative fallback is in sync with fixed coords
    if (isFixedMode(zone) && zone.contains(JsonKeys::FixedX)) {
        syncRelativeFromFixed(zone);
    }

    // Copy z-order if present, otherwise set to end
    if (zoneData.contains(JsonKeys::ZOrder)) {
        zone[JsonKeys::ZOrder] = zoneData[JsonKeys::ZOrder].toInt();
    } else {
        zone[JsonKeys::ZOrder] = m_zones.size();
    }

    if (existingIndex >= 0 && allowIdReuse) {
        // Update existing zone in place (for undo/redo)
        // This prevents QML from seeing the zone disappear and reappear
        m_zones[existingIndex] = zone;

        // Handle signal emission (deferred during batch updates)
        if (m_batchUpdateDepth > 0) {
            // Defer signals until batch completes
            m_pendingColorChanges.insert(zoneId);
            m_pendingZonesChanged = true;
            m_pendingZonesModified = true;
        } else {
            // Emit signals for zone update (not removal/addition)
            Q_EMIT zoneGeometryChanged(zoneId);
            Q_EMIT zoneNameChanged(zoneId);
            Q_EMIT zoneNumberChanged(zoneId);
            Q_EMIT zoneColorChanged(zoneId);
            Q_EMIT zonesChanged();
            Q_EMIT zonesModified();
        }
    } else {
        // Add new zone
        m_zones.append(zone);

        // Handle signal emission (deferred during batch updates)
        if (m_batchUpdateDepth > 0) {
            // Defer signals until batch completes
            m_pendingZoneAdded.insert(zoneId);
            m_pendingZonesChanged = true;
            m_pendingZonesModified = true;
        } else {
            // Immediate signal emission
            Q_EMIT zoneAdded(zoneId);
            Q_EMIT zonesChanged();
            Q_EMIT zonesModified();
        }
    }

    return zoneId;
}

QVariantMap ZoneManager::getZoneById(const QString& zoneId) const
{
    int index = findZoneIndex(zoneId);
    if (index < 0 || index >= m_zones.size()) {
        return QVariantMap();
    }
    return m_zones[index].toMap();
}

void ZoneManager::setZoneData(const QString& zoneId, const QVariantMap& zoneData)
{
    if (zoneId.isEmpty() || zoneData.isEmpty()) {
        qCWarning(lcEditorZone) << "Empty zone ID or data for setZoneData";
        return;
    }

    int index = findZoneIndex(zoneId);
    if (index < 0 || index >= m_zones.size()) {
        qCWarning(lcEditorZone) << "Zone not found for setZoneData:" << zoneId;
        return;
    }

    // Replace zone data completely
    m_zones[index] = zoneData;
    Q_EMIT zonesChanged();
    Q_EMIT zonesModified();
}

void ZoneManager::restoreZones(const QVariantList& zones)
{
    // Validate and deduplicate zones list before restoring
    QSet<QString> zoneIds;
    QSet<int> zoneNumbers;
    QVariantList validated;
    bool hasDuplicates = false;

    for (const QVariant& zoneVar : zones) {
        if (!zoneVar.canConvert<QVariantMap>()) {
            qCWarning(lcEditorZone) << "Invalid zone data type in restoreZones";
            return; // Don't restore if invalid
        }

        QVariantMap zone = zoneVar.toMap();

        // Validate required fields
        if (!zone.contains(JsonKeys::Id) || !zone.contains(JsonKeys::X) || !zone.contains(JsonKeys::Y)
            || !zone.contains(JsonKeys::Width) || !zone.contains(JsonKeys::Height)) {
            qCWarning(lcEditorZone) << "restoreZones: invalid zone data, missing required fields";
            return; // Don't restore if invalid
        }

        // Validate geometry based on geometry mode
        qreal x = zone[JsonKeys::X].toDouble();
        qreal y = zone[JsonKeys::Y].toDouble();
        qreal width = zone[JsonKeys::Width].toDouble();
        qreal height = zone[JsonKeys::Height].toDouble();

        if (isFixedMode(zone)) {
            // Fixed mode: validate relative fallback is present and fixed pixel data is sane
            if (!qIsFinite(x) || !qIsFinite(y) || !qIsFinite(width) || !qIsFinite(height)) {
                qCWarning(lcEditorZone) << "Invalid relative fallback in fixed zone restoreZones:" << x << y << width
                                        << height;
                return;
            }
            QRectF fixedGeo = extractFixedGeometry(zone);
            if (!qIsFinite(fixedGeo.x()) || !qIsFinite(fixedGeo.y()) || !qIsFinite(fixedGeo.width())
                || !qIsFinite(fixedGeo.height()) || fixedGeo.width() <= 0.0 || fixedGeo.height() <= 0.0) {
                qCWarning(lcEditorZone) << "Invalid fixed zone geometry in restoreZones:" << fixedGeo;
                return;
            }
        } else {
            // Relative mode: validate 0-1 range
            if (x < 0.0 || x > 1.0 || y < 0.0 || y > 1.0 || width <= 0.0 || width > 1.0 || height <= 0.0
                || height > 1.0) {
                qCWarning(lcEditorZone) << "Invalid zone geometry in restoreZones:" << x << y << width << height;
                return; // Don't restore if invalid
            }
        }

        // Check for duplicate IDs — skip duplicates, keep first occurrence
        QString zoneId = zone[JsonKeys::Id].toString();
        if (zoneId.isEmpty()) {
            qCWarning(lcEditorZone) << "Empty zone ID in restoreZones";
            return; // Don't restore if invalid
        }
        if (zoneIds.contains(zoneId)) {
            qCWarning(lcEditorZone) << "Duplicate zone ID in restoreZones, skipping:" << zoneId;
            hasDuplicates = true;
            continue; // Skip duplicate, keep first occurrence
        }
        zoneIds.insert(zoneId);

        // Check for duplicate numbers (warn but don't fail - numbers can be renumbered)
        int zoneNumber = zone[JsonKeys::ZoneNumber].toInt();
        if (zoneNumber > 0 && zoneNumbers.contains(zoneNumber)) {
            qCWarning(lcEditorZone) << "Duplicate zone number in restoreZones:" << zoneNumber << "(will be renumbered)";
        }
        if (zoneNumber > 0) {
            zoneNumbers.insert(zoneNumber);
        }

        validated.append(zoneVar);
    }

    // Restore the deduplicated list
    m_zones = hasDuplicates ? validated : zones;
    Q_EMIT zonesChanged();
    Q_EMIT zonesModified();
}
