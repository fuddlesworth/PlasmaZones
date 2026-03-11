// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "../ZoneManager.h"
#include "../../../core/constants.h"
#include "../../../core/logging.h"

#include <QtMath>

using namespace PlasmaZones;

// ═══════════════════════════════════════════════════════════════════════════
// Zone structural operations
// ═══════════════════════════════════════════════════════════════════════════

QString ZoneManager::duplicateZone(const QString& zoneId)
{
    auto zoneOpt = getValidatedZone(zoneId);
    if (!zoneOpt) {
        qCWarning(lcEditorZone) << "Zone not found for duplication:" << zoneId;
        return QString();
    }

    const QVariantMap& srcZone = *zoneOpt;
    QString originalName = srcZone[JsonKeys::Name].toString();
    int zoneNumber = m_zones.size() + 1;
    QString copyName = originalName + QStringLiteral(" (Copy)");

    if (isFixedMode(srcZone)) {
        // Fixed mode: duplicate in pixel space
        QRectF fixedGeo = extractFixedGeometry(srcZone);
        qreal fx = qMax(0.0, fixedGeo.x() + EditorConstants::DuplicateOffsetPixels);
        qreal fy = qMax(0.0, fixedGeo.y() + EditorConstants::DuplicateOffsetPixels);

        QVariantMap duplicate = createZone(copyName, zoneNumber, 0, 0, 0.25, 0.25);
        duplicate[JsonKeys::GeometryMode] = static_cast<int>(ZoneGeometryMode::Fixed);
        duplicate[JsonKeys::FixedX] = fx;
        duplicate[JsonKeys::FixedY] = fy;
        duplicate[JsonKeys::FixedWidth] = fixedGeo.width();
        duplicate[JsonKeys::FixedHeight] = fixedGeo.height();
        syncRelativeFromFixed(duplicate);

        QString newZoneId = duplicate[JsonKeys::Id].toString();
        m_zones.append(duplicate);
        emitZoneSignal(SignalType::ZoneAdded, newZoneId);
        return newZoneId;
    }

    // Relative mode: original behavior
    QRectF original = extractZoneGeometry(srcZone);
    qreal newX = qMin(original.x() + EditorConstants::DuplicateOffset, 1.0 - original.width());
    qreal newY = qMin(original.y() + EditorConstants::DuplicateOffset, 1.0 - original.height());

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

    if (isFixedMode(original)) {
        // Fixed mode: split in pixel space
        QRectF fixedGeo = extractFixedGeometry(original);
        const qreal minFixedSize = static_cast<qreal>(EditorConstants::MinFixedZoneSize);

        // Use qRound to avoid float imprecision rejecting valid splits (e.g. 100/2 = 49.999...)
        qreal splitDim = horizontal ? qRound(fixedGeo.height() / 2.0) : qRound(fixedGeo.width() / 2.0);
        qreal remainDim = horizontal ? (fixedGeo.height() - splitDim) : (fixedGeo.width() - splitDim);
        if (splitDim < minFixedSize || remainDim < minFixedSize) {
            qCWarning(lcEditorZone) << "split fixed zone: resulting zones too small"
                                    << "(dimension:" << (horizontal ? fixedGeo.height() : fixedGeo.width())
                                    << "px, min:" << minFixedSize << "px)";
            return QString();
        }

        // Shrink original
        if (horizontal) {
            original[JsonKeys::FixedHeight] = splitDim;
        } else {
            original[JsonKeys::FixedWidth] = splitDim;
        }
        syncRelativeFromFixed(original);
        m_zones[index] = original;
        emitZoneSignal(SignalType::GeometryChanged, zoneId, false);

        // Create new zone in pixel space
        int zoneNumber = m_zones.size() + 1;
        QVariantMap newZone = createZone(QStringLiteral("Zone %1").arg(zoneNumber), zoneNumber, 0, 0, 0.25, 0.25);
        newZone[JsonKeys::GeometryMode] = static_cast<int>(ZoneGeometryMode::Fixed);
        if (horizontal) {
            newZone[JsonKeys::FixedX] = fixedGeo.x();
            newZone[JsonKeys::FixedY] = fixedGeo.y() + splitDim;
            newZone[JsonKeys::FixedWidth] = fixedGeo.width();
            newZone[JsonKeys::FixedHeight] = remainDim;
        } else {
            newZone[JsonKeys::FixedX] = fixedGeo.x() + splitDim;
            newZone[JsonKeys::FixedY] = fixedGeo.y();
            newZone[JsonKeys::FixedWidth] = remainDim;
            newZone[JsonKeys::FixedHeight] = fixedGeo.height();
        }
        syncRelativeFromFixed(newZone);

        QString newZoneId = newZone[JsonKeys::Id].toString();
        m_zones.append(newZone);
        emitZoneSignal(SignalType::ZoneAdded, newZoneId);
        return newZoneId;
    }

    // Relative mode: original behavior
    QRectF geom = extractZoneGeometry(original);

    // Check if split would create zones smaller than minimum size
    const qreal minSize = EditorConstants::MinZoneSize;
    if (horizontal) {
        qreal newH = geom.height() / 2.0;
        if (newH < minSize) {
            qCWarning(lcEditorZone) << "split horizontally: resulting zones would be too small"
                                    << "(current height:" << geom.height() << ", min size:" << minSize << ")";
            return QString();
        }
    } else {
        qreal newW = geom.width() / 2.0;
        if (newW < minSize) {
            qCWarning(lcEditorZone) << "split vertically: resulting zones would be too small"
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
        newZone = createZone(QStringLiteral("Zone %1").arg(zoneNumber), zoneNumber, geom.x(), geom.y() + newH,
                             geom.width(), newH);
    } else {
        qreal newW = geom.width() / 2.0;
        original[JsonKeys::Width] = newW;
        m_zones[index] = original;
        emitZoneSignal(SignalType::GeometryChanged, zoneId, false);
        newZone = createZone(QStringLiteral("Zone %1").arg(zoneNumber), zoneNumber, geom.x() + newW, geom.y(), newW,
                             geom.height());
    }

    QString newZoneId = newZone[JsonKeys::Id].toString();
    m_zones.append(newZone);

    emitZoneSignal(SignalType::ZoneAdded, newZoneId);

    return newZoneId;
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
