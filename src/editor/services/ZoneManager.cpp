// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ZoneManager.h"
#include "ZoneAutoFiller.h"
#include "../../core/constants.h"

#include <QUuid>
#include <QtMath>
#include <QCoreApplication>
#include <QLatin1String>
#include <algorithm>
#include "../../core/logging.h"

using namespace PlasmaZones;

// ═══════════════════════════════════════════════════════════════════════════════
// DRY HELPER METHOD IMPLEMENTATIONS
// ═══════════════════════════════════════════════════════════════════════════════

QRectF ZoneManager::extractZoneGeometry(const QVariantMap& zone) const
{
    return QRectF(zone[JsonKeys::X].toDouble(),
                  zone[JsonKeys::Y].toDouble(),
                  zone[JsonKeys::Width].toDouble(),
                  zone[JsonKeys::Height].toDouble());
}

std::optional<QVariantMap> ZoneManager::getValidatedZone(const QString& zoneId) const
{
    if (zoneId.isEmpty()) {
        return std::nullopt;
    }

    int index = findZoneIndex(zoneId);
    if (index < 0 || index >= m_zones.size()) {
        return std::nullopt;
    }

    return m_zones[index].toMap();
}

ZoneManager::ValidatedGeometry ZoneManager::validateAndClampGeometry(qreal x, qreal y, qreal width, qreal height) const
{
    ValidatedGeometry result;

    // Input validation
    if (x < 0.0 || x > 1.0 || y < 0.0 || y > 1.0 || width <= 0.0 || width > 1.0 || height <= 0.0 || height > 1.0) {
        result.isValid = false;
        return result;
    }

    // Minimum size
    result.width = qMax(EditorConstants::MinZoneSize, width);
    result.height = qMax(EditorConstants::MinZoneSize, height);

    // Clamp to screen bounds
    result.x = qBound(0.0, x, 1.0 - result.width);
    result.y = qBound(0.0, y, 1.0 - result.height);
    result.isValid = true;

    return result;
}

void ZoneManager::emitZoneSignal(SignalType type, const QString& zoneId, bool includeModified)
{
    if (m_batchUpdateDepth > 0) {
        // Defer signals until batch completes
        switch (type) {
        case SignalType::ZoneAdded:
            m_pendingZoneAdded.insert(zoneId);
            break;
        case SignalType::ZoneRemoved:
            m_pendingZoneRemoved.insert(zoneId);
            break;
        case SignalType::GeometryChanged:
            m_pendingGeometryChanges.insert(zoneId);
            break;
        case SignalType::ColorChanged:
            m_pendingColorChanges.insert(zoneId);
            break;
        default:
            break;
        }
        m_pendingZonesChanged = true;
        if (includeModified) {
            m_pendingZonesModified = true;
        }
    } else {
        // Immediate signal emission
        switch (type) {
        case SignalType::ZoneAdded:
            Q_EMIT zoneAdded(zoneId);
            break;
        case SignalType::ZoneRemoved:
            Q_EMIT zoneRemoved(zoneId);
            break;
        case SignalType::GeometryChanged:
            Q_EMIT zoneGeometryChanged(zoneId);
            break;
        case SignalType::NameChanged:
            Q_EMIT zoneNameChanged(zoneId);
            break;
        case SignalType::NumberChanged:
            Q_EMIT zoneNumberChanged(zoneId);
            break;
        case SignalType::ColorChanged:
            Q_EMIT zoneColorChanged(zoneId);
            break;
        case SignalType::ZOrderChanged:
            Q_EMIT zoneZOrderChanged(zoneId);
            break;
        }
        Q_EMIT zonesChanged();
        if (includeModified) {
            Q_EMIT zonesModified();
        }
    }
}

void ZoneManager::updateAllZOrderValues()
{
    for (int i = 0; i < m_zones.size(); ++i) {
        QVariantMap zoneMap = m_zones[i].toMap();
        zoneMap[JsonKeys::ZOrder] = i;
        m_zones[i] = zoneMap;
    }
}

// ═══════════════════════════════════════════════════════════════════════════════
// CONSTRUCTOR
// ═══════════════════════════════════════════════════════════════════════════════

ZoneManager::ZoneManager(QObject* parent)
    : QObject(parent)
    , m_defaultHighlightColor(QString::fromLatin1(EditorConstants::DefaultHighlightColor))
    , m_defaultInactiveColor(QString::fromLatin1(EditorConstants::DefaultInactiveColor))
    , m_defaultBorderColor(QString::fromLatin1(EditorConstants::DefaultBorderColor))
    , m_autoFiller(std::make_unique<ZoneAutoFiller>(this))
{
}

QVariantMap ZoneManager::createZone(const QString& name, int number, qreal x, qreal y, qreal width, qreal height)
{
    QVariantMap zone;
    zone[JsonKeys::Id] = QUuid::createUuid().toString();
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
    ValidatedGeometry geom = validateAndClampGeometry(x, y, width, height);
    if (!geom.isValid) {
        qCWarning(lcEditorZone) << "Invalid zone geometry:" << x << y << width << height;
        return QString();
    }

    int zoneNumber = m_zones.size() + 1;
    QString zoneName = QStringLiteral("Zone %1").arg(zoneNumber);
    QVariantMap zone = createZone(zoneName, zoneNumber, geom.x, geom.y, geom.width, geom.height);
    QString zoneId = zone[JsonKeys::Id].toString();

    m_zones.append(zone);
    emitZoneSignal(SignalType::ZoneAdded, zoneId);

    return zoneId;
}

void ZoneManager::updateZoneGeometry(const QString& zoneId, qreal x, qreal y, qreal width, qreal height)
{
    if (zoneId.isEmpty()) {
        qCWarning(lcEditorZone) << "Empty zone ID for geometry update";
        return;
    }

    ValidatedGeometry geom = validateAndClampGeometry(x, y, width, height);
    if (!geom.isValid) {
        qCWarning(lcEditorZone) << "Invalid zone geometry:" << x << y << width << height;
        return;
    }

    int index = findZoneIndex(zoneId);
    if (index < 0 || index >= m_zones.size()) {
        qCWarning(lcEditorZone) << "Zone not found for geometry update:" << zoneId;
        return;
    }

    QVariantMap zone = m_zones[index].toMap();
    zone[JsonKeys::X] = geom.x;
    zone[JsonKeys::Y] = geom.y;
    zone[JsonKeys::Width] = geom.width;
    zone[JsonKeys::Height] = geom.height;
    m_zones[index] = zone;

    emitZoneSignal(SignalType::GeometryChanged, zoneId);
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

    ValidatedGeometry geom = validateAndClampGeometry(x, y, width, height);
    if (!geom.isValid) {
        return;
    }

    QVariantMap zone = m_zones[index].toMap();
    zone[JsonKeys::X] = geom.x;
    zone[JsonKeys::Y] = geom.y;
    zone[JsonKeys::Width] = geom.width;
    zone[JsonKeys::Height] = geom.height;
    m_zones[index] = zone;

    // No zonesModified - this is a preview, not a user action
    emitZoneSignal(SignalType::GeometryChanged, zoneId, false);
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
    m_zones.replace(index, zone);

    emitZoneSignal(SignalType::NameChanged, zoneId);
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
    m_zones.replace(index, zone);

    emitZoneSignal(SignalType::NumberChanged, zoneId);
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
    m_zones.replace(index, zone);

    emitZoneSignal(SignalType::ColorChanged, zoneId);
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
    m_zones.replace(index, zone);

    // Reuse ColorChanged signal for all appearance updates
    emitZoneSignal(SignalType::ColorChanged, zoneId);
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

    emitZoneSignal(SignalType::ZoneRemoved, zoneId);
}

QString ZoneManager::duplicateZone(const QString& zoneId)
{
    auto zoneOpt = getValidatedZone(zoneId);
    if (!zoneOpt) {
        qCWarning(lcEditorZone) << "Zone not found for duplication:" << zoneId;
        return QString();
    }

    QRectF original = extractZoneGeometry(*zoneOpt);
    QString originalName = (*zoneOpt)[JsonKeys::Name].toString();

    // Offset slightly, but respect zone dimensions to stay in bounds
    qreal newX = qMin(original.x() + EditorConstants::DuplicateOffset, 1.0 - original.width());
    qreal newY = qMin(original.y() + EditorConstants::DuplicateOffset, 1.0 - original.height());

    int zoneNumber = m_zones.size() + 1;
    QString copyName = originalName + QStringLiteral(" (Copy)");
    QVariantMap duplicate = createZone(copyName, zoneNumber, newX, newY, original.width(), original.height());
    QString newZoneId = duplicate[JsonKeys::Id].toString();

    m_zones.append(duplicate);
    emitZoneSignal(SignalType::ZoneAdded, newZoneId);

    return newZoneId;
}

QString ZoneManager::splitZone(const QString& zoneId, bool horizontal)
{
    auto zoneOpt = getValidatedZone(zoneId);
    if (!zoneOpt) {
        qCWarning(lcEditorZone) << "Zone not found for split:" << zoneId;
        return QString();
    }

    int index = findZoneIndex(zoneId);
    QVariantMap original = *zoneOpt;
    QRectF geom = extractZoneGeometry(original);

    // Check if split would create zones smaller than minimum size
    const qreal minSize = EditorConstants::MinZoneSize;
    if (horizontal) {
        qreal newH = geom.height() / 2.0;
        if (newH < minSize) {
            qCWarning(lcEditorZone) << "Cannot split horizontally - resulting zones would be too small"
                                    << "(current height:" << geom.height() << ", min size:" << minSize << ")";
            return QString();
        }
    } else {
        qreal newW = geom.width() / 2.0;
        if (newW < minSize) {
            qCWarning(lcEditorZone) << "Cannot split vertically - resulting zones would be too small"
                                    << "(current width:" << geom.width() << ", min size:" << minSize << ")";
            return QString();
        }
    }

    int zoneNumber = m_zones.size() + 1;
    QVariantMap newZone;

    if (horizontal) {
        qreal newH = geom.height() / 2.0;
        original[JsonKeys::Height] = newH;
        m_zones[index] = original;
        emitZoneSignal(SignalType::GeometryChanged, zoneId, false);
        newZone = createZone(QStringLiteral("Zone %1").arg(zoneNumber), zoneNumber,
                            geom.x(), geom.y() + newH, geom.width(), newH);
    } else {
        qreal newW = geom.width() / 2.0;
        original[JsonKeys::Width] = newW;
        m_zones[index] = original;
        emitZoneSignal(SignalType::GeometryChanged, zoneId, false);
        newZone = createZone(QStringLiteral("Zone %1").arg(zoneNumber), zoneNumber,
                            geom.x() + newW, geom.y(), newW, geom.height());
    }

    QString newZoneId = newZone[JsonKeys::Id].toString();
    m_zones.append(newZone);

    emitZoneSignal(SignalType::ZoneAdded, newZoneId);

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

    updateAllZOrderValues();
    emitZoneSignal(SignalType::ZOrderChanged, zoneId);
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

    updateAllZOrderValues();
    emitZoneSignal(SignalType::ZOrderChanged, zoneId);
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

    // Update zOrder values for swapped zones only (optimization over updateAllZOrderValues)
    QVariantMap zone1 = m_zones[index].toMap();
    QVariantMap zone2 = m_zones[index + 1].toMap();
    zone1[JsonKeys::ZOrder] = index;
    zone2[JsonKeys::ZOrder] = index + 1;
    m_zones[index] = zone1;
    m_zones[index + 1] = zone2;

    emitZoneSignal(SignalType::ZOrderChanged, zoneId);
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

    // Update zOrder values for swapped zones only (optimization over updateAllZOrderValues)
    QVariantMap zone1 = m_zones[index - 1].toMap();
    QVariantMap zone2 = m_zones[index].toMap();
    zone1[JsonKeys::ZOrder] = index - 1;
    zone2[JsonKeys::ZOrder] = index;
    m_zones[index - 1] = zone1;
    m_zones[index] = zone2;

    emitZoneSignal(SignalType::ZOrderChanged, zoneId);
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
// AUTO-FILL OPERATIONS (delegated to ZoneAutoFiller - SRP)
// ═══════════════════════════════════════════════════════════════════════════

QVariantMap ZoneManager::findAdjacentZones(const QString& zoneId, qreal threshold)
{
    return m_autoFiller->findAdjacentZones(zoneId, threshold);
}

bool ZoneManager::expandToFillSpace(const QString& zoneId, qreal mouseX, qreal mouseY)
{
    return m_autoFiller->expandToFillSpace(zoneId, mouseX, mouseY);
}

QVariantMap ZoneManager::calculateFillRegion(const QString& zoneId, qreal mouseX, qreal mouseY)
{
    return m_autoFiller->calculateFillRegion(zoneId, mouseX, mouseY);
}

void ZoneManager::deleteZoneWithFill(const QString& zoneId, bool autoFill)
{
    m_autoFiller->deleteZoneWithFill(zoneId, autoFill);
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
