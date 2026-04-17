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
    if (!zoneData.contains(::PhosphorZones::ZoneJsonKeys::Id) || !zoneData.contains(::PhosphorZones::ZoneJsonKeys::X)
        || !zoneData.contains(::PhosphorZones::ZoneJsonKeys::Y)
        || !zoneData.contains(::PhosphorZones::ZoneJsonKeys::Width)
        || !zoneData.contains(::PhosphorZones::ZoneJsonKeys::Height)) {
        qCWarning(lcEditorZone) << "PhosphorZones::Zone data: invalid, missing required fields";
        return QString();
    }

    // Validate geometry based on geometry mode
    qreal x = zoneData[::PhosphorZones::ZoneJsonKeys::X].toDouble();
    qreal y = zoneData[::PhosphorZones::ZoneJsonKeys::Y].toDouble();
    qreal width = zoneData[::PhosphorZones::ZoneJsonKeys::Width].toDouble();
    qreal height = zoneData[::PhosphorZones::ZoneJsonKeys::Height].toDouble();

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
    QString zoneId = zoneData[::PhosphorZones::ZoneJsonKeys::Id].toString();
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
    QString name = zoneData.contains(::PhosphorZones::ZoneJsonKeys::Name)
        ? zoneData[::PhosphorZones::ZoneJsonKeys::Name].toString()
        : QString();
    int zoneNumber = zoneData.contains(::PhosphorZones::ZoneJsonKeys::ZoneNumber)
        ? zoneData[::PhosphorZones::ZoneJsonKeys::ZoneNumber].toInt()
        : m_zones.size() + 1;

    QVariantMap zone = createZone(name, zoneNumber, x, y, width, height);

    // Update ID (createZone generates new ID, but we want to preserve paste ID)
    zone[::PhosphorZones::ZoneJsonKeys::Id] = zoneId;

    // Copy all appearance properties
    if (zoneData.contains(::PhosphorZones::ZoneJsonKeys::HighlightColor)) {
        zone[::PhosphorZones::ZoneJsonKeys::HighlightColor] =
            zoneData[::PhosphorZones::ZoneJsonKeys::HighlightColor].toString();
    }
    if (zoneData.contains(::PhosphorZones::ZoneJsonKeys::InactiveColor)) {
        zone[::PhosphorZones::ZoneJsonKeys::InactiveColor] =
            zoneData[::PhosphorZones::ZoneJsonKeys::InactiveColor].toString();
    }
    if (zoneData.contains(::PhosphorZones::ZoneJsonKeys::BorderColor)) {
        zone[::PhosphorZones::ZoneJsonKeys::BorderColor] =
            zoneData[::PhosphorZones::ZoneJsonKeys::BorderColor].toString();
    }
    if (zoneData.contains(::PhosphorZones::ZoneJsonKeys::ActiveOpacity)) {
        zone[::PhosphorZones::ZoneJsonKeys::ActiveOpacity] =
            zoneData[::PhosphorZones::ZoneJsonKeys::ActiveOpacity].toDouble();
    }
    if (zoneData.contains(::PhosphorZones::ZoneJsonKeys::InactiveOpacity)) {
        zone[::PhosphorZones::ZoneJsonKeys::InactiveOpacity] =
            zoneData[::PhosphorZones::ZoneJsonKeys::InactiveOpacity].toDouble();
    }
    if (zoneData.contains(::PhosphorZones::ZoneJsonKeys::BorderWidth)) {
        zone[::PhosphorZones::ZoneJsonKeys::BorderWidth] = zoneData[::PhosphorZones::ZoneJsonKeys::BorderWidth].toInt();
    }
    if (zoneData.contains(::PhosphorZones::ZoneJsonKeys::BorderRadius)) {
        zone[::PhosphorZones::ZoneJsonKeys::BorderRadius] =
            zoneData[::PhosphorZones::ZoneJsonKeys::BorderRadius].toInt();
    }

    QString useCustomColorsKey = QString::fromLatin1(::PhosphorZones::ZoneJsonKeys::UseCustomColors);
    if (zoneData.contains(useCustomColorsKey)) {
        zone[useCustomColorsKey] = zoneData[useCustomColorsKey].toBool();
    }

    // Copy geometry mode and fixed geometry
    if (zoneData.contains(::PhosphorZones::ZoneJsonKeys::GeometryMode)) {
        zone[::PhosphorZones::ZoneJsonKeys::GeometryMode] =
            zoneData[::PhosphorZones::ZoneJsonKeys::GeometryMode].toInt();
    }
    if (zoneData.contains(::PhosphorZones::ZoneJsonKeys::FixedX)) {
        zone[::PhosphorZones::ZoneJsonKeys::FixedX] = zoneData[::PhosphorZones::ZoneJsonKeys::FixedX].toDouble();
        zone[::PhosphorZones::ZoneJsonKeys::FixedY] = zoneData[::PhosphorZones::ZoneJsonKeys::FixedY].toDouble();
        zone[::PhosphorZones::ZoneJsonKeys::FixedWidth] =
            zoneData[::PhosphorZones::ZoneJsonKeys::FixedWidth].toDouble();
        zone[::PhosphorZones::ZoneJsonKeys::FixedHeight] =
            zoneData[::PhosphorZones::ZoneJsonKeys::FixedHeight].toDouble();
    }
    // For fixed zones, ensure relative fallback is in sync with fixed coords
    if (isFixedMode(zone) && zone.contains(::PhosphorZones::ZoneJsonKeys::FixedX)) {
        syncRelativeFromFixed(zone);
    }

    // Copy z-order if present, otherwise set to end
    if (zoneData.contains(::PhosphorZones::ZoneJsonKeys::ZOrder)) {
        zone[::PhosphorZones::ZoneJsonKeys::ZOrder] = zoneData[::PhosphorZones::ZoneJsonKeys::ZOrder].toInt();
    } else {
        zone[::PhosphorZones::ZoneJsonKeys::ZOrder] = m_zones.size();
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
        qCWarning(lcEditorZone) << "PhosphorZones::Zone not found for setZoneData:" << zoneId;
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
        if (!zone.contains(::PhosphorZones::ZoneJsonKeys::Id) || !zone.contains(::PhosphorZones::ZoneJsonKeys::X)
            || !zone.contains(::PhosphorZones::ZoneJsonKeys::Y) || !zone.contains(::PhosphorZones::ZoneJsonKeys::Width)
            || !zone.contains(::PhosphorZones::ZoneJsonKeys::Height)) {
            qCWarning(lcEditorZone) << "restoreZones: invalid zone data, missing required fields";
            return; // Don't restore if invalid
        }

        // Validate geometry based on geometry mode
        qreal x = zone[::PhosphorZones::ZoneJsonKeys::X].toDouble();
        qreal y = zone[::PhosphorZones::ZoneJsonKeys::Y].toDouble();
        qreal width = zone[::PhosphorZones::ZoneJsonKeys::Width].toDouble();
        qreal height = zone[::PhosphorZones::ZoneJsonKeys::Height].toDouble();

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
        QString zoneId = zone[::PhosphorZones::ZoneJsonKeys::Id].toString();
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
        int zoneNumber = zone[::PhosphorZones::ZoneJsonKeys::ZoneNumber].toInt();
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
