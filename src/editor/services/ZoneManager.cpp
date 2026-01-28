// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ZoneManager.h"
#include "../../core/constants.h"

#include <QUuid>
#include <QtMath>
#include <QCoreApplication>
#include <QLatin1String>
#include <algorithm>
#include "../../core/logging.h"

using namespace PlasmaZones;

ZoneManager::ZoneManager(QObject* parent)
    : QObject(parent)
    , m_defaultHighlightColor(QString::fromLatin1(EditorConstants::DefaultHighlightColor))
    , m_defaultInactiveColor(QString::fromLatin1(EditorConstants::DefaultInactiveColor))
    , m_defaultBorderColor(QString::fromLatin1(EditorConstants::DefaultBorderColor))
{
}

QVariantMap ZoneManager::createZone(const QString& name, int number, qreal x, qreal y, qreal width, qreal height)
{
    QVariantMap zone;
    zone[JsonKeys::Id] = QUuid::createUuid().toString(QUuid::WithoutBraces);
    zone[JsonKeys::Name] = name;
    zone[JsonKeys::ZoneNumber] = number;
    zone[JsonKeys::X] = x;
    zone[JsonKeys::Y] = y;
    zone[JsonKeys::Width] = width;
    zone[JsonKeys::Height] = height;
    zone[JsonKeys::ZOrder] = m_zones.size(); // New zones go on top
    // Use settable defaults (theme-based if set, otherwise fallback to constants)
    zone[JsonKeys::HighlightColor] = m_defaultHighlightColor.isEmpty()
        ? QString::fromLatin1(EditorConstants::DefaultHighlightColor)
        : m_defaultHighlightColor;
    zone[JsonKeys::InactiveColor] = m_defaultInactiveColor.isEmpty()
        ? QString::fromLatin1(EditorConstants::DefaultInactiveColor)
        : m_defaultInactiveColor;
    zone[JsonKeys::BorderColor] = m_defaultBorderColor.isEmpty()
        ? QString::fromLatin1(EditorConstants::DefaultBorderColor)
        : m_defaultBorderColor;
    // Initialize appearance properties with defaults
    zone[JsonKeys::ActiveOpacity] = Defaults::Opacity;
    zone[JsonKeys::InactiveOpacity] = Defaults::InactiveOpacity;
    zone[JsonKeys::BorderWidth] = Defaults::BorderWidth;
    zone[JsonKeys::BorderRadius] = Defaults::BorderRadius;
    zone[JsonKeys::UseCustomColors] = false; // New zones use theme colors by default
    return zone;
}

QString ZoneManager::addZone(qreal x, qreal y, qreal width, qreal height)
{
    // Input validation
    if (x < 0.0 || x > 1.0 || y < 0.0 || y > 1.0 || width <= 0.0 || width > 1.0 || height <= 0.0 || height > 1.0) {
        qCWarning(lcEditorZone) << "Invalid zone geometry:" << x << y << width << height;
        return QString();
    }

    // Minimum size check
    width = qMax(EditorConstants::MinZoneSize, width);
    height = qMax(EditorConstants::MinZoneSize, height);

    // Clamp to screen bounds
    x = qBound(0.0, x, 1.0 - width);
    y = qBound(0.0, y, 1.0 - height);

    int zoneNumber = m_zones.size() + 1;
    QString zoneName = QStringLiteral("Zone %1").arg(zoneNumber);
    QVariantMap zone = createZone(zoneName, zoneNumber, x, y, width, height);
    QString zoneId = zone[JsonKeys::Id].toString();

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

    return zoneId;
}

void ZoneManager::updateZoneGeometry(const QString& zoneId, qreal x, qreal y, qreal width, qreal height)
{
    if (zoneId.isEmpty()) {
        qCWarning(lcEditorZone) << "Empty zone ID for geometry update";
        return;
    }

    if (x < 0.0 || x > 1.0 || y < 0.0 || y > 1.0 || width <= 0.0 || width > 1.0 || height <= 0.0 || height > 1.0) {
        qCWarning(lcEditorZone) << "Invalid zone geometry:" << x << y << width << height;
        return;
    }

    int index = findZoneIndex(zoneId);
    if (index < 0 || index >= m_zones.size()) {
        qCWarning(lcEditorZone) << "Zone not found for geometry update:" << zoneId;
        return;
    }

    // Minimum size
    width = qMax(EditorConstants::MinZoneSize, width);
    height = qMax(EditorConstants::MinZoneSize, height);

    // Clamp to screen
    x = qBound(0.0, x, 1.0 - width);
    y = qBound(0.0, y, 1.0 - height);

    // Update zone in list
    QVariantMap zone = m_zones[index].toMap();
    zone[JsonKeys::X] = x;
    zone[JsonKeys::Y] = y;
    zone[JsonKeys::Width] = width;
    zone[JsonKeys::Height] = height;
    m_zones[index] = zone;

    // Handle signal emission (deferred during batch updates)
    if (m_batchUpdateDepth > 0) {
        // Defer signals until batch completes
        m_pendingGeometryChanges.insert(zoneId);
        m_pendingZonesChanged = true;
        m_pendingZonesModified = true;
    } else {
        // Immediate signal emission
        Q_EMIT zoneGeometryChanged(zoneId);
        Q_EMIT zonesChanged();
        Q_EMIT zonesModified();
    }
}

void ZoneManager::updateZoneGeometryDirect(const QString& zoneId, qreal x, qreal y, qreal width, qreal height)
{
    // Direct update without undo commands - used for multi-zone drag preview
    if (zoneId.isEmpty()) {
        return;
    }

    int index = findZoneIndex(zoneId);
    if (index < 0 || index >= m_zones.size()) {
        return;
    }

    // Minimum size
    width = qMax(EditorConstants::MinZoneSize, width);
    height = qMax(EditorConstants::MinZoneSize, height);

    // Clamp to screen
    x = qBound(0.0, x, 1.0 - width);
    y = qBound(0.0, y, 1.0 - height);

    // Update zone in list
    QVariantMap zone = m_zones[index].toMap();
    zone[JsonKeys::X] = x;
    zone[JsonKeys::Y] = y;
    zone[JsonKeys::Width] = width;
    zone[JsonKeys::Height] = height;
    m_zones[index] = zone;

    // Handle signal emission (deferred during batch updates)
    if (m_batchUpdateDepth > 0) {
        // Defer signals until batch completes
        m_pendingGeometryChanges.insert(zoneId);
        m_pendingZonesChanged = true;
        // Note: no zonesModified - this is a preview, not a user action
    } else {
        // Emit only zonesChanged for QML binding update (no zonesModified - not a user action)
        Q_EMIT zoneGeometryChanged(zoneId);
        Q_EMIT zonesChanged();
    }
}

void ZoneManager::updateZoneName(const QString& zoneId, const QString& name)
{
    if (zoneId.isEmpty()) {
        qCWarning(lcEditorZone) << "Empty zone ID for name update";
        return;
    }

    int index = findZoneIndex(zoneId);
    if (index < 0 || index >= m_zones.size()) {
        qCWarning(lcEditorZone) << "Zone not found for name update:" << zoneId;
        return;
    }

    QVariantMap zone = m_zones[index].toMap();
    zone[JsonKeys::Name] = name;
    // Force QVariantList update by replacing the item
    // QML needs replace() to detect the change
    m_zones.replace(index, zone);

    // Handle signal emission (deferred during batch updates)
    if (m_batchUpdateDepth > 0) {
        // Defer signals until batch completes
        m_pendingZonesChanged = true;
        m_pendingZonesModified = true;
    } else {
        // Immediate signal emission
        Q_EMIT zoneNameChanged(zoneId);
        Q_EMIT zonesChanged();
        Q_EMIT zonesModified();
    }
}

void ZoneManager::updateZoneNumber(const QString& zoneId, int number)
{
    if (zoneId.isEmpty() || number < 1) {
        qCWarning(lcEditorZone) << "Invalid zone ID or number:" << zoneId << number;
        return;
    }

    int index = findZoneIndex(zoneId);
    if (index < 0 || index >= m_zones.size()) {
        qCWarning(lcEditorZone) << "Zone not found for number update:" << zoneId;
        return;
    }

    QVariantMap zone = m_zones[index].toMap();
    zone[JsonKeys::ZoneNumber] = number;
    // Force QVariantList update by replacing the item
    // QML needs replace() to detect the change
    m_zones.replace(index, zone);

    // Handle signal emission (deferred during batch updates)
    if (m_batchUpdateDepth > 0) {
        // Defer signals until batch completes
        m_pendingZonesChanged = true;
        m_pendingZonesModified = true;
    } else {
        // Immediate signal emission
        Q_EMIT zoneNumberChanged(zoneId);
        Q_EMIT zonesChanged();
        Q_EMIT zonesModified();
    }
}

void ZoneManager::updateZoneColor(const QString& zoneId, const QString& colorType, const QString& color)
{
    if (zoneId.isEmpty() || colorType.isEmpty() || color.isEmpty()) {
        qCWarning(lcEditorZone) << "Invalid parameters for color update";
        return;
    }

    int index = findZoneIndex(zoneId);
    if (index < 0 || index >= m_zones.size()) {
        qCWarning(lcEditorZone) << "Zone not found for color update:" << zoneId;
        return;
    }

    QVariantMap zone = m_zones[index].toMap();
    zone[colorType] = color;
    // Force QVariantList update by replacing the item
    // QML needs replace() to detect the change
    m_zones.replace(index, zone);

    // Handle signal emission (deferred during batch updates)
    if (m_batchUpdateDepth > 0) {
        // Defer signals until batch completes
        m_pendingColorChanges.insert(zoneId);
        m_pendingZonesChanged = true;
        m_pendingZonesModified = true;
    } else {
        // Immediate signal emission
        Q_EMIT zoneColorChanged(zoneId);
        Q_EMIT zonesChanged(); // Emit zonesChanged to ensure QML bindings update
        Q_EMIT zonesModified();
    }
}

void ZoneManager::updateZoneAppearance(const QString& zoneId, const QString& propertyName, const QVariant& value)
{
    if (zoneId.isEmpty() || propertyName.isEmpty()) {
        qCWarning(lcEditorZone) << "Invalid parameters for appearance update";
        return;
    }

    int index = findZoneIndex(zoneId);
    if (index < 0 || index >= m_zones.size()) {
        qCWarning(lcEditorZone) << "Zone not found for appearance update:" << zoneId;
        return;
    }

    QVariantMap zone = m_zones[index].toMap();

    // Normalize property name to use JsonKeys constants (must match save/load)
    QString normalizedKey = propertyName;
    QString useCustomColorsKey = QString::fromLatin1(JsonKeys::UseCustomColors);

    // Normalize known appearance property names to use JsonKeys constants
    if (propertyName.compare(QLatin1String("useCustomColors"), Qt::CaseInsensitive) == 0
        || propertyName == useCustomColorsKey) {
        normalizedKey = useCustomColorsKey;
    } else if (propertyName.compare(QLatin1String("activeOpacity"), Qt::CaseInsensitive) == 0) {
        normalizedKey = QString::fromLatin1(JsonKeys::ActiveOpacity);
    } else if (propertyName.compare(QLatin1String("inactiveOpacity"), Qt::CaseInsensitive) == 0) {
        normalizedKey = QString::fromLatin1(JsonKeys::InactiveOpacity);
    } else if (propertyName.compare(QLatin1String("borderWidth"), Qt::CaseInsensitive) == 0) {
        normalizedKey = QString::fromLatin1(JsonKeys::BorderWidth);
    } else if (propertyName.compare(QLatin1String("borderRadius"), Qt::CaseInsensitive) == 0) {
        normalizedKey = QString::fromLatin1(JsonKeys::BorderRadius);
    }

    zone[normalizedKey] = value;
    // Force QVariantList update by replacing the item
    // QML needs replace() to detect the change
    m_zones.replace(index, zone);

    // Handle signal emission (deferred during batch updates)
    if (m_batchUpdateDepth > 0) {
        // Defer signals until batch completes
        m_pendingColorChanges.insert(zoneId);
        m_pendingZonesChanged = true;
        m_pendingZonesModified = true;
    } else {
        // Immediate signal emission
        Q_EMIT zoneColorChanged(zoneId); // Reuse zoneColorChanged signal for all appearance updates
        Q_EMIT zonesChanged(); // Emit zonesChanged to ensure QML bindings update
        Q_EMIT zonesModified();
    }
}

void ZoneManager::deleteZone(const QString& zoneId)
{
    if (zoneId.isEmpty()) {
        qCWarning(lcEditorZone) << "Empty zone ID for deletion";
        return;
    }

    int index = findZoneIndex(zoneId);
    if (index < 0 || index >= m_zones.size()) {
        qCWarning(lcEditorZone) << "Zone not found for deletion:" << zoneId;
        return;
    }

    m_zones.removeAt(index);
    renumberZones();

    // Handle signal emission (deferred during batch updates)
    if (m_batchUpdateDepth > 0) {
        // Defer signals until batch completes
        m_pendingZoneRemoved.insert(zoneId);
        m_pendingZonesChanged = true;
        m_pendingZonesModified = true;
    } else {
        // Immediate signal emission
        Q_EMIT zoneRemoved(zoneId);
        Q_EMIT zonesChanged();
        Q_EMIT zonesModified();
    }
}

QString ZoneManager::duplicateZone(const QString& zoneId)
{
    if (zoneId.isEmpty()) {
        qCWarning(lcEditorZone) << "Empty zone ID for duplication";
        return QString();
    }

    int index = findZoneIndex(zoneId);
    if (index < 0 || index >= m_zones.size()) {
        qCWarning(lcEditorZone) << "Zone not found for duplication:" << zoneId;
        return QString();
    }

    QVariantMap original = m_zones[index].toMap();
    qreal x = original[JsonKeys::X].toDouble();
    qreal y = original[JsonKeys::Y].toDouble();
    qreal width = original[JsonKeys::Width].toDouble();
    qreal height = original[JsonKeys::Height].toDouble();
    QString originalName = original[JsonKeys::Name].toString();

    // Offset slightly, but respect zone dimensions to stay in bounds
    qreal newX = x + EditorConstants::DuplicateOffset;
    qreal newY = y + EditorConstants::DuplicateOffset;
    newX = qMin(newX, 1.0 - width);
    newY = qMin(newY, 1.0 - height);

    int zoneNumber = m_zones.size() + 1;
    QString copyName = originalName + QStringLiteral(" (Copy)");
    QVariantMap duplicate = createZone(copyName, zoneNumber, newX, newY, width, height);
    QString newZoneId = duplicate[JsonKeys::Id].toString();

    m_zones.append(duplicate);

    // Handle signal emission (deferred during batch updates)
    if (m_batchUpdateDepth > 0) {
        // Defer signals until batch completes
        m_pendingZoneAdded.insert(newZoneId);
        m_pendingZonesChanged = true;
        m_pendingZonesModified = true;
    } else {
        // Immediate signal emission
        Q_EMIT zoneAdded(newZoneId);
        Q_EMIT zonesChanged();
        Q_EMIT zonesModified();
    }

    return newZoneId;
}

QString ZoneManager::splitZone(const QString& zoneId, bool horizontal)
{
    if (zoneId.isEmpty()) {
        qCWarning(lcEditorZone) << "Empty zone ID for split";
        return QString();
    }

    int index = findZoneIndex(zoneId);
    if (index < 0 || index >= m_zones.size()) {
        qCWarning(lcEditorZone) << "Zone not found for split:" << zoneId;
        return QString();
    }

    QVariantMap original = m_zones[index].toMap();
    qreal x = original[JsonKeys::X].toDouble();
    qreal y = original[JsonKeys::Y].toDouble();
    qreal w = original[JsonKeys::Width].toDouble();
    qreal h = original[JsonKeys::Height].toDouble();

    // Check if split would create zones smaller than minimum size
    const qreal minSize = EditorConstants::MinZoneSize;
    if (horizontal) {
        qreal newH = h / 2.0;
        if (newH < minSize) {
            qCWarning(lcEditorZone) << "Cannot split horizontally - resulting zones would be too small"
                                    << "(current height:" << h << ", min size:" << minSize << ")";
            return QString();
        }
    } else {
        qreal newW = w / 2.0;
        if (newW < minSize) {
            qCWarning(lcEditorZone) << "Cannot split vertically - resulting zones would be too small"
                                    << "(current width:" << w << ", min size:" << minSize << ")";
            return QString();
        }
    }

    int zoneNumber = m_zones.size() + 1;
    QVariantMap newZone;

    if (horizontal) {
        qreal newH = h / 2.0;
        original[JsonKeys::Height] = newH;
        m_zones[index] = original;
        Q_EMIT zoneGeometryChanged(zoneId);
        newZone = createZone(QStringLiteral("Zone %1").arg(zoneNumber), zoneNumber, x, y + newH, w, newH);
    } else {
        qreal newW = w / 2.0;
        original[JsonKeys::Width] = newW;
        m_zones[index] = original;
        Q_EMIT zoneGeometryChanged(zoneId);
        newZone = createZone(QStringLiteral("Zone %1").arg(zoneNumber), zoneNumber, x + newW, y, newW, h);
    }

    QString newZoneId = newZone[JsonKeys::Id].toString();
    m_zones.append(newZone);

    Q_EMIT zoneAdded(newZoneId);
    Q_EMIT zonesChanged();
    Q_EMIT zonesModified();

    return newZoneId;
}

QVariantList ZoneManager::getZonesSharingEdge(const QString& zoneId, qreal edgeX, qreal edgeY, qreal threshold)
{
    QVariantList result;

    int index = findZoneIndex(zoneId);
    if (index < 0 || index >= m_zones.size()) {
        qCDebug(lcEditorZone) << "Zone not found:" << zoneId;
        return result;
    }

    QVariantMap zone1 = m_zones[index].toMap();
    qreal z1x = zone1[JsonKeys::X].toDouble();
    qreal z1y = zone1[JsonKeys::Y].toDouble();
    qreal z1w = zone1[JsonKeys::Width].toDouble();
    qreal z1h = zone1[JsonKeys::Height].toDouble();

    bool checkRightEdge = qAbs(edgeX - (z1x + z1w)) < threshold;
    bool checkBottomEdge = qAbs(edgeY - (z1y + z1h)) < threshold;

    for (int i = 0; i < m_zones.size(); ++i) {
        if (i == index)
            continue;

        QVariantMap zone2 = m_zones[i].toMap();
        qreal z2x = zone2[JsonKeys::X].toDouble();
        qreal z2y = zone2[JsonKeys::Y].toDouble();
        qreal z2w = zone2[JsonKeys::Width].toDouble();
        qreal z2h = zone2[JsonKeys::Height].toDouble();

        bool sharesEdge = false;

        if (checkRightEdge) {
            if (qAbs((z1x + z1w) - z2x) < threshold) {
                if (z1y < (z2y + z2h) && (z1y + z1h) > z2y) {
                    sharesEdge = true;
                }
            }
        }

        if (checkBottomEdge) {
            if (qAbs((z1y + z1h) - z2y) < threshold) {
                if (z1x < (z2x + z2w) && (z1x + z1w) > z2x) {
                    sharesEdge = true;
                }
            }
        }

        if (sharesEdge) {
            QVariantMap zoneInfo;
            zoneInfo[JsonKeys::Id] = zone2[JsonKeys::Id].toString();
            zoneInfo[JsonKeys::X] = z2x;
            zoneInfo[JsonKeys::Y] = z2y;
            zoneInfo[JsonKeys::Width] = z2w;
            zoneInfo[JsonKeys::Height] = z2h;
            result.append(zoneInfo);
        }
    }

    return result;
}

QVector<QPair<QString, QRectF>> ZoneManager::collectGeometriesAtDivider(const QString& zoneId1, const QString& zoneId2,
                                                                        bool isVertical)
{
    QVector<QPair<QString, QRectF>> result;
    int index1 = findZoneIndex(zoneId1);
    int index2 = findZoneIndex(zoneId2);

    if (index1 < 0 || index1 >= m_zones.size() || index2 < 0 || index2 >= m_zones.size()) {
        return result;
    }

    QVariantMap zone1 = m_zones[index1].toMap();
    QVariantMap zone2 = m_zones[index2].toMap();

    qreal z1x = zone1[JsonKeys::X].toDouble();
    qreal z1y = zone1[JsonKeys::Y].toDouble();
    qreal z1w = zone1[JsonKeys::Width].toDouble();
    qreal z2x = zone2[JsonKeys::X].toDouble();
    qreal z2y = zone2[JsonKeys::Y].toDouble();
    qreal z2h = zone2[JsonKeys::Height].toDouble();

    qreal oldDividerPos = 0;
    const qreal threshold = EditorConstants::EdgeThreshold;

    if (isVertical) {
        if (z1x < z2x) {
            oldDividerPos = z1x + z1w;
        } else {
            oldDividerPos = z2x + zone2[JsonKeys::Width].toDouble();
        }

        QList<int> leftZones;
        QList<int> rightZones;
        // Pre-allocate capacity (performance optimization)
        leftZones.reserve(m_zones.size() / 2);
        rightZones.reserve(m_zones.size() / 2);

        for (int i = 0; i < m_zones.size(); ++i) {
            const QVariantMap zone = m_zones[i].toMap();
            const qreal zx = zone[JsonKeys::X].toDouble();
            const qreal zw = zone[JsonKeys::Width].toDouble();
            const qreal rightEdge = zx + zw;

            if (qAbs(rightEdge - oldDividerPos) < threshold) {
                leftZones.append(i);
            } else if (qAbs(zx - oldDividerPos) < threshold) {
                rightZones.append(i);
            }
        }

        for (int idx : leftZones) {
            const QVariantMap zone = m_zones[idx].toMap();
            const QString zoneId = zone[JsonKeys::Id].toString();
            const qreal x = zone[JsonKeys::X].toDouble();
            const qreal y = zone[JsonKeys::Y].toDouble();
            const qreal w = zone[JsonKeys::Width].toDouble();
            const qreal h = zone[JsonKeys::Height].toDouble();
            result.append(qMakePair(zoneId, QRectF(x, y, w, h)));
        }
        for (int idx : rightZones) {
            const QVariantMap zone = m_zones[idx].toMap();
            const QString zoneId = zone[JsonKeys::Id].toString();
            const qreal x = zone[JsonKeys::X].toDouble();
            const qreal y = zone[JsonKeys::Y].toDouble();
            const qreal w = zone[JsonKeys::Width].toDouble();
            const qreal h = zone[JsonKeys::Height].toDouble();
            result.append(qMakePair(zoneId, QRectF(x, y, w, h)));
        }
    } else {
        if (z1y < z2y) {
            oldDividerPos = z1y + zone1[JsonKeys::Height].toDouble();
        } else {
            oldDividerPos = z2y + z2h;
        }

        QList<int> topZones;
        QList<int> bottomZones;
        // Pre-allocate capacity (performance optimization)
        topZones.reserve(m_zones.size() / 2);
        bottomZones.reserve(m_zones.size() / 2);

        for (int i = 0; i < m_zones.size(); ++i) {
            const QVariantMap zone = m_zones[i].toMap();
            const qreal zy = zone[JsonKeys::Y].toDouble();
            const qreal zh = zone[JsonKeys::Height].toDouble();
            const qreal bottomEdge = zy + zh;

            if (qAbs(bottomEdge - oldDividerPos) < threshold) {
                topZones.append(i);
            } else if (qAbs(zy - oldDividerPos) < threshold) {
                bottomZones.append(i);
            }
        }

        for (int idx : topZones) {
            const QVariantMap zone = m_zones[idx].toMap();
            const QString zoneId = zone[JsonKeys::Id].toString();
            const qreal x = zone[JsonKeys::X].toDouble();
            const qreal y = zone[JsonKeys::Y].toDouble();
            const qreal w = zone[JsonKeys::Width].toDouble();
            const qreal h = zone[JsonKeys::Height].toDouble();
            result.append(qMakePair(zoneId, QRectF(x, y, w, h)));
        }
        for (int idx : bottomZones) {
            const QVariantMap zone = m_zones[idx].toMap();
            const QString zoneId = zone[JsonKeys::Id].toString();
            const qreal x = zone[JsonKeys::X].toDouble();
            const qreal y = zone[JsonKeys::Y].toDouble();
            const qreal w = zone[JsonKeys::Width].toDouble();
            const qreal h = zone[JsonKeys::Height].toDouble();
            result.append(qMakePair(zoneId, QRectF(x, y, w, h)));
        }
    }

    return result;
}

void ZoneManager::resizeZonesAtDivider(const QString& zoneId1, const QString& zoneId2, qreal newDividerX,
                                       qreal newDividerY, bool isVertical)
{
    int index1 = findZoneIndex(zoneId1);
    int index2 = findZoneIndex(zoneId2);

    if (index1 < 0 || index1 >= m_zones.size() || index2 < 0 || index2 >= m_zones.size()) {
        qCWarning(lcEditorZone) << "Invalid zone IDs for divider resize";
        return;
    }

    QVariantMap zone1 = m_zones[index1].toMap();
    QVariantMap zone2 = m_zones[index2].toMap();

    qreal z1x = zone1[JsonKeys::X].toDouble();
    qreal z1y = zone1[JsonKeys::Y].toDouble();
    qreal z1w = zone1[JsonKeys::Width].toDouble();
    qreal z2x = zone2[JsonKeys::X].toDouble();
    qreal z2y = zone2[JsonKeys::Y].toDouble();
    qreal z2h = zone2[JsonKeys::Height].toDouble();

    qreal oldDividerPos = 0;
    qreal threshold = EditorConstants::EdgeThreshold;

    if (isVertical) {
        if (z1x < z2x) {
            oldDividerPos = z1x + z1w;
        } else {
            oldDividerPos = z2x + zone2[JsonKeys::Width].toDouble();
        }

        QList<int> leftZones;
        QList<int> rightZones;
        // Pre-allocate capacity (performance optimization)
        leftZones.reserve(m_zones.size() / 2);
        rightZones.reserve(m_zones.size() / 2);

        for (int i = 0; i < m_zones.size(); ++i) {
            const QVariantMap zone = m_zones[i].toMap();
            const qreal zx = zone[JsonKeys::X].toDouble();
            const qreal zw = zone[JsonKeys::Width].toDouble();
            const qreal rightEdge = zx + zw;

            if (qAbs(rightEdge - oldDividerPos) < threshold) {
                leftZones.append(i);
            } else if (qAbs(zx - oldDividerPos) < threshold) {
                rightZones.append(i);
            }
        }

        qreal deltaX = newDividerX - oldDividerPos;
        bool valid = true;
        const qreal minSize = EditorConstants::MinZoneSize;

        for (int idx : leftZones) {
            QVariantMap zone = m_zones[idx].toMap();
            qreal newWidth = zone[JsonKeys::Width].toDouble() + deltaX;
            if (newWidth < minSize) {
                valid = false;
                break;
            }
            qreal zx = zone[JsonKeys::X].toDouble();
            if (zx + newWidth > 1.0) {
                valid = false;
                break;
            }
        }

        if (valid) {
            for (int idx : rightZones) {
                QVariantMap zone = m_zones[idx].toMap();
                qreal zx = zone[JsonKeys::X].toDouble();
                qreal zw = zone[JsonKeys::Width].toDouble();
                qreal newX = zx + deltaX;
                qreal newWidth = zw - deltaX;

                if (newWidth < minSize) {
                    valid = false;
                    break;
                }
                if (newX < 0.0 || newX + newWidth > 1.0) {
                    valid = false;
                    break;
                }
            }
        }

        if (!valid) {
            qCWarning(lcEditorZone) << "Divider resize would create invalid zones";
            return;
        }

        for (int idx : leftZones) {
            QVariantMap zone = m_zones[idx].toMap();
            QString zoneId = zone[JsonKeys::Id].toString();
            qreal zx = zone[JsonKeys::X].toDouble();
            qreal newWidth = newDividerX - zx;

            if (newWidth < minSize) {
                newWidth = minSize;
            }

            zone[JsonKeys::Width] = newWidth;
            m_zones[idx] = zone;
            if (m_batchUpdateDepth > 0) {
                m_pendingGeometryChanges.insert(zoneId);
                m_pendingZonesChanged = true;
                m_pendingZonesModified = true;
            } else {
                Q_EMIT zoneGeometryChanged(zoneId);
            }
        }

        for (int idx : rightZones) {
            QVariantMap zone = m_zones[idx].toMap();
            QString zoneId = zone[JsonKeys::Id].toString();
            qreal oldX = zone[JsonKeys::X].toDouble();
            qreal oldW = zone[JsonKeys::Width].toDouble();
            qreal newX = newDividerX;
            qreal newWidth = (oldX + oldW) - newX;

            if (newWidth < minSize) {
                newWidth = minSize;
                newX = (oldX + oldW) - minSize;
            }

            zone[JsonKeys::X] = newX;
            zone[JsonKeys::Width] = newWidth;
            m_zones[idx] = zone;
            if (m_batchUpdateDepth > 0) {
                m_pendingGeometryChanges.insert(zoneId);
                m_pendingZonesChanged = true;
                m_pendingZonesModified = true;
            } else {
                Q_EMIT zoneGeometryChanged(zoneId);
            }
        }
    } else {
        // Horizontal divider - similar logic for Y/Height
        if (z1y < z2y) {
            oldDividerPos = z1y + zone1[JsonKeys::Height].toDouble();
        } else {
            oldDividerPos = z2y + z2h;
        }

        QList<int> topZones;
        QList<int> bottomZones;
        // Pre-allocate capacity (performance optimization)
        topZones.reserve(m_zones.size() / 2);
        bottomZones.reserve(m_zones.size() / 2);

        for (int i = 0; i < m_zones.size(); ++i) {
            const QVariantMap zone = m_zones[i].toMap();
            const qreal zy = zone[JsonKeys::Y].toDouble();
            const qreal zh = zone[JsonKeys::Height].toDouble();
            const qreal bottomEdge = zy + zh;

            if (qAbs(bottomEdge - oldDividerPos) < threshold) {
                topZones.append(i);
            } else if (qAbs(zy - oldDividerPos) < threshold) {
                bottomZones.append(i);
            }
        }

        qreal deltaY = newDividerY - oldDividerPos;
        bool valid = true;
        const qreal minSize = EditorConstants::MinZoneSize;

        for (int idx : topZones) {
            QVariantMap zone = m_zones[idx].toMap();
            qreal newHeight = zone[JsonKeys::Height].toDouble() + deltaY;
            if (newHeight < minSize) {
                valid = false;
                break;
            }
            qreal zy = zone[JsonKeys::Y].toDouble();
            if (zy + newHeight > 1.0) {
                valid = false;
                break;
            }
        }

        if (valid) {
            for (int idx : bottomZones) {
                QVariantMap zone = m_zones[idx].toMap();
                qreal zy = zone[JsonKeys::Y].toDouble();
                qreal zh = zone[JsonKeys::Height].toDouble();
                qreal newY = zy + deltaY;
                qreal newHeight = zh - deltaY;

                if (newHeight < minSize) {
                    valid = false;
                    break;
                }
                if (newY < 0.0 || newY + newHeight > 1.0) {
                    valid = false;
                    break;
                }
            }
        }

        if (!valid) {
            qCWarning(lcEditorZone) << "Divider resize would create invalid zones";
            return;
        }

        for (int idx : topZones) {
            QVariantMap zone = m_zones[idx].toMap();
            QString zoneId = zone[JsonKeys::Id].toString();
            qreal zy = zone[JsonKeys::Y].toDouble();
            qreal newHeight = newDividerY - zy;

            if (newHeight < minSize) {
                newHeight = minSize;
            }

            zone[JsonKeys::Height] = newHeight;
            m_zones[idx] = zone;
            if (m_batchUpdateDepth > 0) {
                m_pendingGeometryChanges.insert(zoneId);
                m_pendingZonesChanged = true;
                m_pendingZonesModified = true;
            } else {
                Q_EMIT zoneGeometryChanged(zoneId);
            }
        }

        for (int idx : bottomZones) {
            QVariantMap zone = m_zones[idx].toMap();
            QString zoneId = zone[JsonKeys::Id].toString();
            qreal oldY = zone[JsonKeys::Y].toDouble();
            qreal oldH = zone[JsonKeys::Height].toDouble();
            qreal newY = newDividerY;
            qreal newHeight = (oldY + oldH) - newY;

            if (newHeight < minSize) {
                newHeight = minSize;
                newY = (oldY + oldH) - minSize;
            }

            zone[JsonKeys::Y] = newY;
            zone[JsonKeys::Height] = newHeight;
            m_zones[idx] = zone;
            if (m_batchUpdateDepth > 0) {
                m_pendingGeometryChanges.insert(zoneId);
                m_pendingZonesChanged = true;
                m_pendingZonesModified = true;
            } else {
                Q_EMIT zoneGeometryChanged(zoneId);
            }
        }
    }

    if (m_batchUpdateDepth == 0) {
        Q_EMIT zonesChanged();
        Q_EMIT zonesModified();
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// Z-ORDER OPERATIONS
// ═══════════════════════════════════════════════════════════════════════════

void ZoneManager::bringToFront(const QString& zoneId)
{
    if (zoneId.isEmpty()) {
        qCWarning(lcEditorZone) << "Empty zone ID for bring to front";
        return;
    }

    int index = findZoneIndex(zoneId);
    if (index < 0 || index >= m_zones.size()) {
        qCWarning(lcEditorZone) << "Zone not found for bring to front:" << zoneId;
        return;
    }

    // Already at front
    if (index == m_zones.size() - 1) {
        return;
    }

    // Move zone to end of list (highest z-order)
    QVariant zone = m_zones.takeAt(index);
    m_zones.append(zone);

    // Update zOrder values for all zones
    for (int i = 0; i < m_zones.size(); ++i) {
        QVariantMap zoneMap = m_zones[i].toMap();
        zoneMap[JsonKeys::ZOrder] = i;
        m_zones[i] = zoneMap;
    }

    Q_EMIT zoneZOrderChanged(zoneId);
    Q_EMIT zonesChanged();
    Q_EMIT zonesModified();
}

void ZoneManager::sendToBack(const QString& zoneId)
{
    if (zoneId.isEmpty()) {
        qCWarning(lcEditorZone) << "Empty zone ID for send to back";
        return;
    }

    int index = findZoneIndex(zoneId);
    if (index < 0 || index >= m_zones.size()) {
        qCWarning(lcEditorZone) << "Zone not found for send to back:" << zoneId;
        return;
    }

    // Already at back
    if (index == 0) {
        return;
    }

    // Move zone to beginning of list (lowest z-order)
    QVariant zone = m_zones.takeAt(index);
    m_zones.prepend(zone);

    // Update zOrder values for all zones
    for (int i = 0; i < m_zones.size(); ++i) {
        QVariantMap zoneMap = m_zones[i].toMap();
        zoneMap[JsonKeys::ZOrder] = i;
        m_zones[i] = zoneMap;
    }

    Q_EMIT zoneZOrderChanged(zoneId);
    Q_EMIT zonesChanged();
    Q_EMIT zonesModified();
}

void ZoneManager::bringForward(const QString& zoneId)
{
    if (zoneId.isEmpty()) {
        qCWarning(lcEditorZone) << "Empty zone ID for bring forward";
        return;
    }

    int index = findZoneIndex(zoneId);
    if (index < 0 || index >= m_zones.size()) {
        qCWarning(lcEditorZone) << "Zone not found for bring forward:" << zoneId;
        return;
    }

    // Already at front
    if (index == m_zones.size() - 1) {
        return;
    }

    // Swap with next zone (move up one layer)
    m_zones.swapItemsAt(index, index + 1);

    // Update zOrder values for swapped zones
    QVariantMap zone1 = m_zones[index].toMap();
    QVariantMap zone2 = m_zones[index + 1].toMap();
    zone1[JsonKeys::ZOrder] = index;
    zone2[JsonKeys::ZOrder] = index + 1;
    m_zones[index] = zone1;
    m_zones[index + 1] = zone2;

    Q_EMIT zoneZOrderChanged(zoneId);
    Q_EMIT zonesChanged();
    Q_EMIT zonesModified();
}

void ZoneManager::sendBackward(const QString& zoneId)
{
    if (zoneId.isEmpty()) {
        qCWarning(lcEditorZone) << "Empty zone ID for send backward";
        return;
    }

    int index = findZoneIndex(zoneId);
    if (index < 0 || index >= m_zones.size()) {
        qCWarning(lcEditorZone) << "Zone not found for send backward:" << zoneId;
        return;
    }

    // Already at back
    if (index == 0) {
        return;
    }

    // Swap with previous zone (move down one layer)
    m_zones.swapItemsAt(index, index - 1);

    // Update zOrder values for swapped zones
    QVariantMap zone1 = m_zones[index - 1].toMap();
    QVariantMap zone2 = m_zones[index].toMap();
    zone1[JsonKeys::ZOrder] = index - 1;
    zone2[JsonKeys::ZOrder] = index;
    m_zones[index - 1] = zone1;
    m_zones[index] = zone2;

    Q_EMIT zoneZOrderChanged(zoneId);
    Q_EMIT zonesChanged();
    Q_EMIT zonesModified();
}

void ZoneManager::clearAllZones()
{
    m_zones.clear();
    Q_EMIT zonesChanged();
    Q_EMIT zonesModified();
}

void ZoneManager::setZones(const QVariantList& zones)
{
    m_zones = zones;
    Q_EMIT zonesChanged();
}

QVariantList ZoneManager::zones() const
{
    return m_zones;
}

int ZoneManager::findZoneIndex(const QString& zoneId) const
{
    for (int i = 0; i < m_zones.size(); ++i) {
        QVariantMap zone = m_zones[i].toMap();
        if (zone[JsonKeys::Id].toString() == zoneId) {
            return i;
        }
    }
    return -1;
}

void ZoneManager::renumberZones()
{
    for (int i = 0; i < m_zones.size(); ++i) {
        QVariantMap zone = m_zones[i].toMap();
        QString zoneId = zone[JsonKeys::Id].toString();
        int oldNumber = zone[JsonKeys::ZoneNumber].toInt();
        int newNumber = i + 1;

        if (oldNumber != newNumber) {
            zone[JsonKeys::ZoneNumber] = newNumber;
            m_zones[i] = zone;
            Q_EMIT zoneNumberChanged(zoneId);
        }
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// AUTO-FILL OPERATIONS
// ═══════════════════════════════════════════════════════════════════════════

bool ZoneManager::isRectangleEmpty(qreal x, qreal y, qreal width, qreal height, const QString& excludeZoneId) const
{
    const qreal threshold = 0.002; // Small epsilon for floating point comparison

    for (const QVariant& zoneVar : m_zones) {
        QVariantMap zone = zoneVar.toMap();
        QString zoneId = zone[JsonKeys::Id].toString();

        if (!excludeZoneId.isEmpty() && zoneId == excludeZoneId) {
            continue;
        }

        qreal zx = zone[JsonKeys::X].toDouble();
        qreal zy = zone[JsonKeys::Y].toDouble();
        qreal zw = zone[JsonKeys::Width].toDouble();
        qreal zh = zone[JsonKeys::Height].toDouble();

        // Check if rectangles overlap (with small threshold to avoid false positives)
        bool overlapsX = (x + threshold < zx + zw) && (x + width - threshold > zx);
        bool overlapsY = (y + threshold < zy + zh) && (y + height - threshold > zy);

        if (overlapsX && overlapsY) {
            return false;
        }
    }

    return true;
}

qreal ZoneManager::findMaxExpansion(const QString& zoneId, int direction) const
{
    int index = findZoneIndex(zoneId);
    if (index < 0 || index >= m_zones.size()) {
        return 0.0;
    }

    QVariantMap zone = m_zones[index].toMap();
    qreal zx = zone[JsonKeys::X].toDouble();
    qreal zy = zone[JsonKeys::Y].toDouble();
    qreal zw = zone[JsonKeys::Width].toDouble();
    qreal zh = zone[JsonKeys::Height].toDouble();

    const qreal step = 0.01; // 1% increments
    qreal maxExpansion = 0.0;

    switch (direction) {
    case 0: // Left
        for (qreal expansion = step; expansion <= zx; expansion += step) {
            if (isRectangleEmpty(zx - expansion, zy, expansion, zh, zoneId)) {
                maxExpansion = expansion;
            } else {
                break;
            }
        }
        break;

    case 1: // Right
        for (qreal expansion = step; expansion <= (1.0 - zx - zw); expansion += step) {
            if (isRectangleEmpty(zx + zw, zy, expansion, zh, zoneId)) {
                maxExpansion = expansion;
            } else {
                break;
            }
        }
        break;

    case 2: // Up
        for (qreal expansion = step; expansion <= zy; expansion += step) {
            if (isRectangleEmpty(zx, zy - expansion, zw, expansion, zoneId)) {
                maxExpansion = expansion;
            } else {
                break;
            }
        }
        break;

    case 3: // Down
        for (qreal expansion = step; expansion <= (1.0 - zy - zh); expansion += step) {
            if (isRectangleEmpty(zx, zy + zh, zw, expansion, zoneId)) {
                maxExpansion = expansion;
            } else {
                break;
            }
        }
        break;
    }

    return maxExpansion;
}

QVariantMap ZoneManager::findAdjacentZones(const QString& zoneId, qreal threshold)
{
    QVariantMap result;
    QVariantList leftZones, rightZones, topZones, bottomZones;

    int index = findZoneIndex(zoneId);
    if (index < 0 || index >= m_zones.size()) {
        result[QStringLiteral("left")] = leftZones;
        result[QStringLiteral("right")] = rightZones;
        result[QStringLiteral("top")] = topZones;
        result[QStringLiteral("bottom")] = bottomZones;
        return result;
    }

    QVariantMap targetZone = m_zones[index].toMap();
    qreal tx = targetZone[JsonKeys::X].toDouble();
    qreal ty = targetZone[JsonKeys::Y].toDouble();
    qreal tw = targetZone[JsonKeys::Width].toDouble();
    qreal th = targetZone[JsonKeys::Height].toDouble();

    for (int i = 0; i < m_zones.size(); ++i) {
        if (i == index)
            continue;

        QVariantMap zone = m_zones[i].toMap();
        QString otherZoneId = zone[JsonKeys::Id].toString();
        qreal zx = zone[JsonKeys::X].toDouble();
        qreal zy = zone[JsonKeys::Y].toDouble();
        qreal zw = zone[JsonKeys::Width].toDouble();
        qreal zh = zone[JsonKeys::Height].toDouble();

        // Check vertical overlap
        bool verticalOverlap = (ty < zy + zh - threshold) && (ty + th > zy + threshold);

        // Check horizontal overlap
        bool horizontalOverlap = (tx < zx + zw - threshold) && (tx + tw > zx + threshold);

        // Left adjacency: zone's right edge touches target's left edge
        if (verticalOverlap && qAbs((zx + zw) - tx) < threshold) {
            leftZones.append(otherZoneId);
        }

        // Right adjacency: zone's left edge touches target's right edge
        if (verticalOverlap && qAbs(zx - (tx + tw)) < threshold) {
            rightZones.append(otherZoneId);
        }

        // Top adjacency: zone's bottom edge touches target's top edge
        if (horizontalOverlap && qAbs((zy + zh) - ty) < threshold) {
            topZones.append(otherZoneId);
        }

        // Bottom adjacency: zone's top edge touches target's bottom edge
        if (horizontalOverlap && qAbs(zy - (ty + th)) < threshold) {
            bottomZones.append(otherZoneId);
        }
    }

    result[QStringLiteral("left")] = leftZones;
    result[QStringLiteral("right")] = rightZones;
    result[QStringLiteral("top")] = topZones;
    result[QStringLiteral("bottom")] = bottomZones;

    return result;
}

bool ZoneManager::expandToFillSpace(const QString& zoneId, qreal mouseX, qreal mouseY)
{
    int index = findZoneIndex(zoneId);
    if (index < 0 || index >= m_zones.size()) {
        qCWarning(lcEditorZone) << "Zone not found for expansion:" << zoneId;
        return false;
    }

    // If mouse coordinates are provided (fill-on-drop), use smartFillZone
    // for proper edge alignment with adjacent zones
    bool hasMousePosition = (mouseX >= 0 && mouseX <= 1 && mouseY >= 0 && mouseY <= 1);
    if (hasMousePosition) {
        return smartFillZone(zoneId, mouseX, mouseY);
    }

    // No mouse position - use directional expansion (programmatic expansion)
    QVariantMap zone = m_zones[index].toMap();
    qreal x = zone[JsonKeys::X].toDouble();
    qreal y = zone[JsonKeys::Y].toDouble();
    qreal w = zone[JsonKeys::Width].toDouble();
    qreal h = zone[JsonKeys::Height].toDouble();

    // First, check if this zone overlaps with any other zones
    // If so, we need to find empty space first
    bool hasOverlap = false;
    for (int i = 0; i < m_zones.size(); ++i) {
        if (i == index)
            continue;

        QVariantMap other = m_zones[i].toMap();
        qreal ox = other[JsonKeys::X].toDouble();
        qreal oy = other[JsonKeys::Y].toDouble();
        qreal ow = other[JsonKeys::Width].toDouble();
        qreal oh = other[JsonKeys::Height].toDouble();

        // Check for overlap
        bool overlapsX = (x < ox + ow) && (x + w > ox);
        bool overlapsY = (y < oy + oh) && (y + h > oy);

        if (overlapsX && overlapsY) {
            hasOverlap = true;
            break;
        }
    }

    if (hasOverlap) {
        // Use smart fill with zone center
        return smartFillZone(zoneId, -1, -1);
    }

    bool changed = false;

    // Try expanding in each direction
    // Left
    qreal leftExpansion = findMaxExpansion(zoneId, 0);
    if (leftExpansion > 0.005) { // At least 0.5% expansion
        x -= leftExpansion;
        w += leftExpansion;
        changed = true;
    }

    // Right
    qreal rightExpansion = findMaxExpansion(zoneId, 1);
    if (rightExpansion > 0.005) {
        w += rightExpansion;
        changed = true;
    }

    // Up
    qreal upExpansion = findMaxExpansion(zoneId, 2);
    if (upExpansion > 0.005) {
        y -= upExpansion;
        h += upExpansion;
        changed = true;
    }

    // Down
    qreal downExpansion = findMaxExpansion(zoneId, 3);
    if (downExpansion > 0.005) {
        h += downExpansion;
        changed = true;
    }

    if (changed) {
        // Clamp to bounds
        x = qBound(0.0, x, 1.0 - EditorConstants::MinZoneSize);
        y = qBound(0.0, y, 1.0 - EditorConstants::MinZoneSize);
        w = qMin(w, 1.0 - x);
        h = qMin(h, 1.0 - y);

        zone[JsonKeys::X] = x;
        zone[JsonKeys::Y] = y;
        zone[JsonKeys::Width] = w;
        zone[JsonKeys::Height] = h;
        m_zones[index] = zone;

        Q_EMIT zoneGeometryChanged(zoneId);
        Q_EMIT zonesChanged();
        Q_EMIT zonesModified();
    }

    return changed;
}

bool ZoneManager::smartFillZone(const QString& zoneId, qreal mouseX, qreal mouseY)
{
    int index = findZoneIndex(zoneId);
    if (index < 0 || index >= m_zones.size()) {
        return false;
    }

    QVariantMap zone = m_zones[index].toMap();
    qreal zx = zone[JsonKeys::X].toDouble();
    qreal zy = zone[JsonKeys::Y].toDouble();
    qreal zw = zone[JsonKeys::Width].toDouble();
    qreal zh = zone[JsonKeys::Height].toDouble();

    // Use mouse position if provided, otherwise use zone center
    qreal targetX = (mouseX >= 0 && mouseX <= 1) ? mouseX : (zx + zw / 2.0);
    qreal targetY = (mouseY >= 0 && mouseY <= 1) ? mouseY : (zy + zh / 2.0);

    // Find the largest empty rectangular region containing the target point
    // so the zone fills where the user clicked.

    // Helper to check if coordinate already exists (with tolerance)
    auto coordExists = [](const QList<qreal>& list, qreal val) {
        for (qreal v : list) {
            if (qAbs(v - val) < 0.001)
                return true;
        }
        return false;
    };

    // Collect all unique X and Y coordinates (zone edges + screen edges)
    QList<qreal> xCoords = {0.0, 1.0};
    QList<qreal> yCoords = {0.0, 1.0};

    for (int i = 0; i < m_zones.size(); ++i) {
        if (i == index)
            continue;

        QVariantMap other = m_zones[i].toMap();
        qreal ox = other[JsonKeys::X].toDouble();
        qreal oy = other[JsonKeys::Y].toDouble();
        qreal ow = other[JsonKeys::Width].toDouble();
        qreal oh = other[JsonKeys::Height].toDouble();

        if (!coordExists(xCoords, ox))
            xCoords.append(ox);
        if (!coordExists(xCoords, ox + ow))
            xCoords.append(ox + ow);
        if (!coordExists(yCoords, oy))
            yCoords.append(oy);
        if (!coordExists(yCoords, oy + oh))
            yCoords.append(oy + oh);
    }

    std::sort(xCoords.begin(), xCoords.end());
    std::sort(yCoords.begin(), yCoords.end());

    // Find the largest empty region that CONTAINS the target point
    qreal bestX = 0, bestY = 0, bestW = 0, bestH = 0;
    qreal bestArea = -1;

    // Check all possible rectangles formed by any pair of X and Y coordinates
    for (int xi1 = 0; xi1 < xCoords.size(); ++xi1) {
        for (int xi2 = xi1 + 1; xi2 < xCoords.size(); ++xi2) {
            for (int yi1 = 0; yi1 < yCoords.size(); ++yi1) {
                for (int yi2 = yi1 + 1; yi2 < yCoords.size(); ++yi2) {
                    qreal rx = xCoords[xi1];
                    qreal ry = yCoords[yi1];
                    qreal rw = xCoords[xi2] - rx;
                    qreal rh = yCoords[yi2] - ry;

                    // Skip regions that are too small
                    if (rw < EditorConstants::MinZoneSize || rh < EditorConstants::MinZoneSize) {
                        continue;
                    }

                    // Check if this region CONTAINS the target point
                    bool containsTarget = (targetX >= rx && targetX <= rx + rw && targetY >= ry && targetY <= ry + rh);
                    if (!containsTarget) {
                        continue;
                    }

                    // Check if this region is empty (no zones overlap with it)
                    if (!isRectangleEmpty(rx, ry, rw, rh, zoneId)) {
                        continue;
                    }

                    qreal area = rw * rh;

                    // Pick the largest region containing the target point
                    if (area > bestArea) {
                        bestArea = area;
                        bestX = rx;
                        bestY = ry;
                        bestW = rw;
                        bestH = rh;
                    }
                }
            }
        }
    }

    if (bestW < EditorConstants::MinZoneSize || bestH < EditorConstants::MinZoneSize) {
        return false;
    }

    // Update the zone
    zone[JsonKeys::X] = bestX;
    zone[JsonKeys::Y] = bestY;
    zone[JsonKeys::Width] = bestW;
    zone[JsonKeys::Height] = bestH;
    m_zones[index] = zone;

    Q_EMIT zoneGeometryChanged(zoneId);
    Q_EMIT zonesChanged();
    Q_EMIT zonesModified();

    return true;
}

QVariantMap ZoneManager::calculateFillRegion(const QString& zoneId, qreal mouseX, qreal mouseY)
{
    int index = findZoneIndex(zoneId);
    if (index < 0 || index >= m_zones.size()) {
        return QVariantMap();
    }

    // Use mouse position as target point
    qreal targetX = mouseX;
    qreal targetY = mouseY;

    // Helper to check if coordinate already exists (with tolerance)
    auto coordExists = [](const QList<qreal>& list, qreal val) {
        for (qreal v : list) {
            if (qAbs(v - val) < 0.001)
                return true;
        }
        return false;
    };

    // Collect all unique X and Y coordinates (zone edges + screen edges)
    QList<qreal> xCoords = {0.0, 1.0};
    QList<qreal> yCoords = {0.0, 1.0};

    for (int i = 0; i < m_zones.size(); ++i) {
        if (i == index)
            continue;

        QVariantMap other = m_zones[i].toMap();
        qreal ox = other[JsonKeys::X].toDouble();
        qreal oy = other[JsonKeys::Y].toDouble();
        qreal ow = other[JsonKeys::Width].toDouble();
        qreal oh = other[JsonKeys::Height].toDouble();

        if (!coordExists(xCoords, ox))
            xCoords.append(ox);
        if (!coordExists(xCoords, ox + ow))
            xCoords.append(ox + ow);
        if (!coordExists(yCoords, oy))
            yCoords.append(oy);
        if (!coordExists(yCoords, oy + oh))
            yCoords.append(oy + oh);
    }

    std::sort(xCoords.begin(), xCoords.end());
    std::sort(yCoords.begin(), yCoords.end());

    // Find the largest empty region that CONTAINS the target point
    qreal bestX = 0, bestY = 0, bestW = 0, bestH = 0;
    qreal bestArea = -1;

    for (int xi1 = 0; xi1 < xCoords.size(); ++xi1) {
        for (int xi2 = xi1 + 1; xi2 < xCoords.size(); ++xi2) {
            for (int yi1 = 0; yi1 < yCoords.size(); ++yi1) {
                for (int yi2 = yi1 + 1; yi2 < yCoords.size(); ++yi2) {
                    qreal rx = xCoords[xi1];
                    qreal ry = yCoords[yi1];
                    qreal rw = xCoords[xi2] - rx;
                    qreal rh = yCoords[yi2] - ry;

                    if (rw < EditorConstants::MinZoneSize || rh < EditorConstants::MinZoneSize) {
                        continue;
                    }

                    // Check if this region CONTAINS the target point
                    bool containsTarget = (targetX >= rx && targetX <= rx + rw && targetY >= ry && targetY <= ry + rh);
                    if (!containsTarget) {
                        continue;
                    }

                    // Check if this region is empty (no zones overlap with it)
                    // Use the provided zone geometry for the exclusion check
                    bool isEmpty = true;
                    for (int i = 0; i < m_zones.size(); ++i) {
                        if (i == index)
                            continue;

                        QVariantMap other = m_zones[i].toMap();
                        qreal ox = other[JsonKeys::X].toDouble();
                        qreal oy = other[JsonKeys::Y].toDouble();
                        qreal ow = other[JsonKeys::Width].toDouble();
                        qreal oh = other[JsonKeys::Height].toDouble();

                        bool overlapsX = (rx < ox + ow - 0.001) && (rx + rw > ox + 0.001);
                        bool overlapsY = (ry < oy + oh - 0.001) && (ry + rh > oy + 0.001);

                        if (overlapsX && overlapsY) {
                            isEmpty = false;
                            break;
                        }
                    }

                    if (!isEmpty)
                        continue;

                    qreal area = rw * rh;
                    if (area > bestArea) {
                        bestArea = area;
                        bestX = rx;
                        bestY = ry;
                        bestW = rw;
                        bestH = rh;
                    }
                }
            }
        }
    }

    if (bestW < EditorConstants::MinZoneSize || bestH < EditorConstants::MinZoneSize) {
        return QVariantMap();
    }

    QVariantMap result;
    result[JsonKeys::X] = bestX;
    result[JsonKeys::Y] = bestY;
    result[JsonKeys::Width] = bestW;
    result[JsonKeys::Height] = bestH;
    return result;
}

void ZoneManager::deleteZoneWithFill(const QString& zoneId, bool autoFill)
{
    if (zoneId.isEmpty()) {
        qCWarning(lcEditorZone) << "Empty zone ID for deletion";
        return;
    }

    int index = findZoneIndex(zoneId);
    if (index < 0 || index >= m_zones.size()) {
        qCWarning(lcEditorZone) << "Zone not found for deletion:" << zoneId;
        return;
    }

    // Get the zone's geometry before deleting
    QVariantMap deletedZone = m_zones[index].toMap();
    qreal dx = deletedZone[JsonKeys::X].toDouble();
    qreal dy = deletedZone[JsonKeys::Y].toDouble();
    qreal dw = deletedZone[JsonKeys::Width].toDouble();
    qreal dh = deletedZone[JsonKeys::Height].toDouble();

    // Find adjacent zones before deletion
    QVariantMap adjacentZones;
    if (autoFill) {
        adjacentZones = findAdjacentZones(zoneId);
    }

    // Delete the zone
    m_zones.removeAt(index);
    renumberZones();

    Q_EMIT zoneRemoved(zoneId);

    // Auto-fill: expand adjacent zones to fill the gap
    if (autoFill) {
        // Strategy: Try to expand adjacent zones to fill the deleted space
        // Priority: larger adjacent zones first, then by direction (left/right before up/down)

        const qreal threshold = 0.02;

        // Get adjacent zone IDs
        QVariantList leftZones = adjacentZones[QStringLiteral("left")].toList();
        QVariantList rightZones = adjacentZones[QStringLiteral("right")].toList();
        QVariantList topZones = adjacentZones[QStringLiteral("top")].toList();
        QVariantList bottomZones = adjacentZones[QStringLiteral("bottom")].toList();

        // Try to expand right zones leftward (into the deleted space)
        for (const QVariant& rightZoneVar : rightZones) {
            QString rightZoneId = rightZoneVar.toString();
            int rightIndex = findZoneIndex(rightZoneId);
            if (rightIndex < 0)
                continue;

            QVariantMap rightZone = m_zones[rightIndex].toMap();
            qreal rx = rightZone[JsonKeys::X].toDouble();
            qreal ry = rightZone[JsonKeys::Y].toDouble();
            qreal rh = rightZone[JsonKeys::Height].toDouble();

            // Check if this zone can expand leftward into deleted space
            // Zone must span the full height of deleted zone or be contained within it
            if (ry >= dy - threshold && ry + rh <= dy + dh + threshold) {
                // Expand leftward
                qreal newX = dx;
                qreal expansion = rx - dx;
                if (expansion > 0) {
                    rightZone[JsonKeys::X] = newX;
                    rightZone[JsonKeys::Width] = rightZone[JsonKeys::Width].toDouble() + expansion;
                    m_zones[rightIndex] = rightZone;
                    Q_EMIT zoneGeometryChanged(rightZoneId);
                }
            }
        }

        // Try to expand left zones rightward
        for (const QVariant& leftZoneVar : leftZones) {
            QString leftZoneId = leftZoneVar.toString();
            int leftIndex = findZoneIndex(leftZoneId);
            if (leftIndex < 0)
                continue;

            QVariantMap leftZone = m_zones[leftIndex].toMap();
            qreal lx = leftZone[JsonKeys::X].toDouble();
            qreal ly = leftZone[JsonKeys::Y].toDouble();
            qreal lw = leftZone[JsonKeys::Width].toDouble();
            qreal lh = leftZone[JsonKeys::Height].toDouble();

            // Check if this zone can expand rightward
            if (ly >= dy - threshold && ly + lh <= dy + dh + threshold) {
                qreal newRight = dx + dw;
                qreal expansion = newRight - (lx + lw);
                if (expansion > 0 && isRectangleEmpty(lx + lw, ly, expansion, lh, leftZoneId)) {
                    leftZone[JsonKeys::Width] = lw + expansion;
                    m_zones[leftIndex] = leftZone;
                    Q_EMIT zoneGeometryChanged(leftZoneId);
                }
            }
        }

        // Try to expand bottom zones upward
        for (const QVariant& bottomZoneVar : bottomZones) {
            QString bottomZoneId = bottomZoneVar.toString();
            int bottomIndex = findZoneIndex(bottomZoneId);
            if (bottomIndex < 0)
                continue;

            QVariantMap bottomZone = m_zones[bottomIndex].toMap();
            qreal bx = bottomZone[JsonKeys::X].toDouble();
            qreal by = bottomZone[JsonKeys::Y].toDouble();
            qreal bw = bottomZone[JsonKeys::Width].toDouble();

            // Check if this zone can expand upward
            if (bx >= dx - threshold && bx + bw <= dx + dw + threshold) {
                qreal expansion = by - dy;
                if (expansion > 0 && isRectangleEmpty(bx, dy, bw, expansion, bottomZoneId)) {
                    bottomZone[JsonKeys::Y] = dy;
                    bottomZone[JsonKeys::Height] = bottomZone[JsonKeys::Height].toDouble() + expansion;
                    m_zones[bottomIndex] = bottomZone;
                    Q_EMIT zoneGeometryChanged(bottomZoneId);
                }
            }
        }

        // Try to expand top zones downward
        for (const QVariant& topZoneVar : topZones) {
            QString topZoneId = topZoneVar.toString();
            int topIndex = findZoneIndex(topZoneId);
            if (topIndex < 0)
                continue;

            QVariantMap topZone = m_zones[topIndex].toMap();
            qreal tx = topZone[JsonKeys::X].toDouble();
            qreal ty = topZone[JsonKeys::Y].toDouble();
            qreal tw = topZone[JsonKeys::Width].toDouble();
            qreal th = topZone[JsonKeys::Height].toDouble();

            // Check if this zone can expand downward
            if (tx >= dx - threshold && tx + tw <= dx + dw + threshold) {
                qreal newBottom = dy + dh;
                qreal expansion = newBottom - (ty + th);
                if (expansion > 0 && isRectangleEmpty(tx, ty + th, tw, expansion, topZoneId)) {
                    topZone[JsonKeys::Height] = th + expansion;
                    m_zones[topIndex] = topZone;
                    Q_EMIT zoneGeometryChanged(topZoneId);
                }
            }
        }
    }

    Q_EMIT zonesChanged();
    Q_EMIT zonesModified();
}

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
        qCWarning(lcEditorZone) << "Invalid zone data - missing required fields";
        return QString();
    }

    // Validate geometry
    qreal x = zoneData[JsonKeys::X].toDouble();
    qreal y = zoneData[JsonKeys::Y].toDouble();
    qreal width = zoneData[JsonKeys::Width].toDouble();
    qreal height = zoneData[JsonKeys::Height].toDouble();

    if (x < 0.0 || x > 1.0 || y < 0.0 || y > 1.0 || width <= 0.0 || width > 1.0 || height <= 0.0 || height > 1.0) {
        qCWarning(lcEditorZone) << "Invalid zone geometry for addZoneFromMap";
        return QString();
    }

    // Use provided ID or generate new one
    QString zoneId = zoneData[JsonKeys::Id].toString();
    int existingIndex = -1;
    if (zoneId.isEmpty()) {
        // ID is empty, generate new one
        zoneId = QUuid::createUuid().toString(QUuid::WithoutBraces);
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
                zoneId = QUuid::createUuid().toString(QUuid::WithoutBraces);
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

    if (zoneData.contains(JsonKeys::Shortcut)) {
        zone[JsonKeys::Shortcut] = zoneData[JsonKeys::Shortcut].toString();
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
    // Validate zones list before restoring
    QSet<QString> zoneIds;
    QSet<int> zoneNumbers;

    for (const QVariant& zoneVar : zones) {
        if (!zoneVar.canConvert<QVariantMap>()) {
            qCWarning(lcEditorZone) << "Invalid zone data type in restoreZones";
            return; // Don't restore if invalid
        }

        QVariantMap zone = zoneVar.toMap();

        // Validate required fields
        if (!zone.contains(JsonKeys::Id) || !zone.contains(JsonKeys::X) || !zone.contains(JsonKeys::Y)
            || !zone.contains(JsonKeys::Width) || !zone.contains(JsonKeys::Height)) {
            qCWarning(lcEditorZone) << "Invalid zone data in restoreZones - missing required fields";
            return; // Don't restore if invalid
        }

        // Validate geometry
        qreal x = zone[JsonKeys::X].toDouble();
        qreal y = zone[JsonKeys::Y].toDouble();
        qreal width = zone[JsonKeys::Width].toDouble();
        qreal height = zone[JsonKeys::Height].toDouble();

        if (x < 0.0 || x > 1.0 || y < 0.0 || y > 1.0 || width <= 0.0 || width > 1.0 || height <= 0.0 || height > 1.0) {
            qCWarning(lcEditorZone) << "Invalid zone geometry in restoreZones:" << x << y << width << height;
            return; // Don't restore if invalid
        }

        // Check for duplicate IDs
        QString zoneId = zone[JsonKeys::Id].toString();
        if (zoneId.isEmpty()) {
            qCWarning(lcEditorZone) << "Empty zone ID in restoreZones";
            return; // Don't restore if invalid
        }
        if (zoneIds.contains(zoneId)) {
            qCWarning(lcEditorZone) << "Duplicate zone ID in restoreZones:" << zoneId;
            return; // Don't restore if duplicates
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
    }

    // All validation passed, restore zones
    m_zones = zones;
    Q_EMIT zonesChanged();
    Q_EMIT zonesModified();
}

// ═══════════════════════════════════════════════════════════════════════════════
// BATCH UPDATE SUPPORT
// ═══════════════════════════════════════════════════════════════════════════════

void ZoneManager::beginBatchUpdate()
{
    ++m_batchUpdateDepth;
}

void ZoneManager::endBatchUpdate()
{
    if (m_batchUpdateDepth > 0) {
        --m_batchUpdateDepth;
        if (m_batchUpdateDepth == 0) {
            emitDeferredSignals();
        }
    }
}

void ZoneManager::emitDeferredSignals()
{
    // Emit zone added signals
    for (const QString& zoneId : std::as_const(m_pendingZoneAdded)) {
        Q_EMIT zoneAdded(zoneId);
    }
    m_pendingZoneAdded.clear();

    // Emit zone removed signals
    for (const QString& zoneId : std::as_const(m_pendingZoneRemoved)) {
        Q_EMIT zoneRemoved(zoneId);
    }
    m_pendingZoneRemoved.clear();

    // Emit geometry change signals for each affected zone
    for (const QString& zoneId : std::as_const(m_pendingGeometryChanges)) {
        Q_EMIT zoneGeometryChanged(zoneId);
    }
    m_pendingGeometryChanges.clear();

    // Emit color change signals for each affected zone
    for (const QString& zoneId : std::as_const(m_pendingColorChanges)) {
        Q_EMIT zoneColorChanged(zoneId);
    }
    m_pendingColorChanges.clear();

    // Emit aggregate signals once at the end
    if (m_pendingZonesChanged) {
        Q_EMIT zonesChanged();
        m_pendingZonesChanged = false;
    }

    if (m_pendingZonesModified) {
        Q_EMIT zonesModified();
        m_pendingZonesModified = false;
    }
}
