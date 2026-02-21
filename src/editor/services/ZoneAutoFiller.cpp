// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ZoneAutoFiller.h"
#include "ZoneManager.h"
#include "../../core/constants.h"
#include "../../core/logging.h"

#include <QtMath>
#include <algorithm>

using namespace PlasmaZones;

ZoneAutoFiller::ZoneAutoFiller(ZoneManager* manager, QObject* parent)
    : QObject(parent)
    , m_manager(manager)
{
}

void ZoneAutoFiller::applyRelativeGeometry(const QString& zoneId, qreal rx, qreal ry, qreal rw, qreal rh)
{
    QVariantMap z = m_manager->getZoneById(zoneId);
    if (z.isEmpty()) return;
    int mode = z.value(QLatin1String("geometryMode"), 0).toInt();
    if (mode == static_cast<int>(ZoneGeometryMode::Fixed)) {
        QSize ss = m_manager->referenceScreenSize();
        qreal sw = qMax(1.0, static_cast<qreal>(ss.width()));
        qreal sh = qMax(1.0, static_cast<qreal>(ss.height()));
        m_manager->updateZoneGeometry(zoneId, rx * sw, ry * sh, rw * sw, rh * sh);
    } else {
        m_manager->updateZoneGeometry(zoneId, rx, ry, rw, rh);
    }
}

bool ZoneAutoFiller::isRectangleEmpty(const QRectF& rect, const QString& excludeZoneId) const
{
    const qreal threshold = 0.002; // Small epsilon for floating point comparison
    const QVariantList& zones = m_manager->zones();

    for (const QVariant& zoneVar : zones) {
        QVariantMap zone = zoneVar.toMap();
        QString zoneId = zone[JsonKeys::Id].toString();

        if (!excludeZoneId.isEmpty() && zoneId == excludeZoneId) {
            continue;
        }

        QRectF zoneRect = m_manager->extractZoneGeometry(zone);

        // Check if rectangles overlap (with small threshold to avoid false positives)
        bool overlapsX = (rect.x() + threshold < zoneRect.right()) && (rect.right() - threshold > zoneRect.x());
        bool overlapsY = (rect.y() + threshold < zoneRect.bottom()) && (rect.bottom() - threshold > zoneRect.y());

        if (overlapsX && overlapsY) {
            return false;
        }
    }

    return true;
}

qreal ZoneAutoFiller::findMaxExpansion(const QString& zoneId, int direction) const
{
    auto zoneOpt = m_manager->getValidatedZone(zoneId);
    if (!zoneOpt) {
        return 0.0;
    }

    QRectF zoneRect = m_manager->extractZoneGeometry(*zoneOpt);
    const qreal step = 0.01; // 1% increments
    qreal maxExpansion = 0.0;

    switch (direction) {
    case 0: // Left
        for (qreal expansion = step; expansion <= zoneRect.x(); expansion += step) {
            QRectF testRect(zoneRect.x() - expansion, zoneRect.y(), expansion, zoneRect.height());
            if (isRectangleEmpty(testRect, zoneId)) {
                maxExpansion = expansion;
            } else {
                break;
            }
        }
        break;

    case 1: // Right
        for (qreal expansion = step; expansion <= (1.0 - zoneRect.right()); expansion += step) {
            QRectF testRect(zoneRect.right(), zoneRect.y(), expansion, zoneRect.height());
            if (isRectangleEmpty(testRect, zoneId)) {
                maxExpansion = expansion;
            } else {
                break;
            }
        }
        break;

    case 2: // Up
        for (qreal expansion = step; expansion <= zoneRect.y(); expansion += step) {
            QRectF testRect(zoneRect.x(), zoneRect.y() - expansion, zoneRect.width(), expansion);
            if (isRectangleEmpty(testRect, zoneId)) {
                maxExpansion = expansion;
            } else {
                break;
            }
        }
        break;

    case 3: // Down
        for (qreal expansion = step; expansion <= (1.0 - zoneRect.bottom()); expansion += step) {
            QRectF testRect(zoneRect.x(), zoneRect.bottom(), zoneRect.width(), expansion);
            if (isRectangleEmpty(testRect, zoneId)) {
                maxExpansion = expansion;
            } else {
                break;
            }
        }
        break;
    }

    return maxExpansion;
}

QVariantMap ZoneAutoFiller::findAdjacentZones(const QString& zoneId, qreal threshold) const
{
    QVariantMap result;
    QVariantList leftZones, rightZones, topZones, bottomZones;

    auto targetOpt = m_manager->getValidatedZone(zoneId);
    if (!targetOpt) {
        result[QStringLiteral("left")] = leftZones;
        result[QStringLiteral("right")] = rightZones;
        result[QStringLiteral("top")] = topZones;
        result[QStringLiteral("bottom")] = bottomZones;
        return result;
    }

    QRectF targetRect = m_manager->extractZoneGeometry(*targetOpt);
    const QVariantList& zones = m_manager->zones();
    int targetIndex = m_manager->findZoneIndex(zoneId);

    for (int i = 0; i < zones.size(); ++i) {
        if (i == targetIndex)
            continue;

        QVariantMap zone = zones[i].toMap();
        QString otherZoneId = zone[JsonKeys::Id].toString();
        QRectF zoneRect = m_manager->extractZoneGeometry(zone);

        // Check vertical overlap
        bool verticalOverlap = (targetRect.y() < zoneRect.bottom() - threshold)
                            && (targetRect.bottom() > zoneRect.y() + threshold);

        // Check horizontal overlap
        bool horizontalOverlap = (targetRect.x() < zoneRect.right() - threshold)
                              && (targetRect.right() > zoneRect.x() + threshold);

        // Left adjacency: zone's right edge touches target's left edge
        if (verticalOverlap && qAbs(zoneRect.right() - targetRect.x()) < threshold) {
            leftZones.append(otherZoneId);
        }

        // Right adjacency: zone's left edge touches target's right edge
        if (verticalOverlap && qAbs(zoneRect.x() - targetRect.right()) < threshold) {
            rightZones.append(otherZoneId);
        }

        // Top adjacency: zone's bottom edge touches target's top edge
        if (horizontalOverlap && qAbs(zoneRect.bottom() - targetRect.y()) < threshold) {
            topZones.append(otherZoneId);
        }

        // Bottom adjacency: zone's top edge touches target's bottom edge
        if (horizontalOverlap && qAbs(zoneRect.y() - targetRect.bottom()) < threshold) {
            bottomZones.append(otherZoneId);
        }
    }

    result[QStringLiteral("left")] = leftZones;
    result[QStringLiteral("right")] = rightZones;
    result[QStringLiteral("top")] = topZones;
    result[QStringLiteral("bottom")] = bottomZones;

    return result;
}

QRectF ZoneAutoFiller::findBestEmptyRegion(qreal targetX, qreal targetY, int excludeZoneIndex) const
{
    const QVariantList& zones = m_manager->zones();

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

    QString excludeZoneId;
    if (excludeZoneIndex >= 0 && excludeZoneIndex < zones.size()) {
        excludeZoneId = zones[excludeZoneIndex].toMap()[JsonKeys::Id].toString();
    }

    for (int i = 0; i < zones.size(); ++i) {
        if (i == excludeZoneIndex)
            continue;

        QRectF rect = m_manager->extractZoneGeometry(zones[i].toMap());

        if (!coordExists(xCoords, rect.x()))
            xCoords.append(rect.x());
        if (!coordExists(xCoords, rect.right()))
            xCoords.append(rect.right());
        if (!coordExists(yCoords, rect.y()))
            yCoords.append(rect.y());
        if (!coordExists(yCoords, rect.bottom()))
            yCoords.append(rect.bottom());
    }

    std::sort(xCoords.begin(), xCoords.end());
    std::sort(yCoords.begin(), yCoords.end());

    // Find the largest empty region that CONTAINS the target point
    QRectF bestRegion;
    qreal bestArea = -1;

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
                    bool containsTarget = (targetX >= rx && targetX <= rx + rw
                                        && targetY >= ry && targetY <= ry + rh);
                    if (!containsTarget) {
                        continue;
                    }

                    // Check if this region is empty
                    QRectF testRect(rx, ry, rw, rh);
                    if (!isRectangleEmpty(testRect, excludeZoneId)) {
                        continue;
                    }

                    qreal area = rw * rh;
                    if (area > bestArea) {
                        bestArea = area;
                        bestRegion = testRect;
                    }
                }
            }
        }
    }

    return bestRegion;
}

bool ZoneAutoFiller::expandToFillSpace(const QString& zoneId, qreal mouseX, qreal mouseY)
{
    auto zoneOpt = m_manager->getValidatedZone(zoneId);
    if (!zoneOpt) {
        qCWarning(lcEditorZone) << "Zone not found for expansion:" << zoneId;
        return false;
    }

    // If mouse coordinates are provided (fill-on-drop), use smartFillZone
    bool hasMousePosition = (mouseX >= 0 && mouseX <= 1 && mouseY >= 0 && mouseY <= 1);
    if (hasMousePosition) {
        return smartFillZone(zoneId, mouseX, mouseY);
    }

    // No mouse position - use directional expansion
    QRectF zoneRect = m_manager->extractZoneGeometry(*zoneOpt);
    const QVariantList& zones = m_manager->zones();
    int index = m_manager->findZoneIndex(zoneId);

    // Check if this zone overlaps with any other zones
    bool hasOverlap = false;
    for (int i = 0; i < zones.size(); ++i) {
        if (i == index)
            continue;

        QRectF otherRect = m_manager->extractZoneGeometry(zones[i].toMap());
        if (zoneRect.intersects(otherRect)) {
            hasOverlap = true;
            break;
        }
    }

    if (hasOverlap) {
        return smartFillZone(zoneId, -1, -1);
    }

    bool changed = false;
    qreal x = zoneRect.x();
    qreal y = zoneRect.y();
    qreal w = zoneRect.width();
    qreal h = zoneRect.height();

    // Try expanding in each direction
    qreal leftExpansion = findMaxExpansion(zoneId, 0);
    if (leftExpansion > 0.005) {
        x -= leftExpansion;
        w += leftExpansion;
        changed = true;
    }

    qreal rightExpansion = findMaxExpansion(zoneId, 1);
    if (rightExpansion > 0.005) {
        w += rightExpansion;
        changed = true;
    }

    qreal upExpansion = findMaxExpansion(zoneId, 2);
    if (upExpansion > 0.005) {
        y -= upExpansion;
        h += upExpansion;
        changed = true;
    }

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

        applyRelativeGeometry(zoneId, x, y, w, h);
    }

    return changed;
}

bool ZoneAutoFiller::smartFillZone(const QString& zoneId, qreal mouseX, qreal mouseY)
{
    int index = m_manager->findZoneIndex(zoneId);
    if (index < 0) {
        return false;
    }

    auto zoneOpt = m_manager->getValidatedZone(zoneId);
    if (!zoneOpt) {
        return false;
    }

    QRectF zoneRect = m_manager->extractZoneGeometry(*zoneOpt);

    // Use mouse position if provided, otherwise use zone center
    qreal targetX = (mouseX >= 0 && mouseX <= 1) ? mouseX : zoneRect.center().x();
    qreal targetY = (mouseY >= 0 && mouseY <= 1) ? mouseY : zoneRect.center().y();

    QRectF bestRegion = findBestEmptyRegion(targetX, targetY, index);

    if (!bestRegion.isValid() || bestRegion.width() < EditorConstants::MinZoneSize
        || bestRegion.height() < EditorConstants::MinZoneSize) {
        return false;
    }

    applyRelativeGeometry(zoneId, bestRegion.x(), bestRegion.y(),
                          bestRegion.width(), bestRegion.height());
    return true;
}

QVariantMap ZoneAutoFiller::calculateFillRegion(const QString& zoneId, qreal mouseX, qreal mouseY) const
{
    int index = m_manager->findZoneIndex(zoneId);
    if (index < 0) {
        return QVariantMap();
    }

    QRectF bestRegion = findBestEmptyRegion(mouseX, mouseY, index);

    if (!bestRegion.isValid() || bestRegion.width() < EditorConstants::MinZoneSize
        || bestRegion.height() < EditorConstants::MinZoneSize) {
        return QVariantMap();
    }

    QVariantMap result;
    result[JsonKeys::X] = bestRegion.x();
    result[JsonKeys::Y] = bestRegion.y();
    result[JsonKeys::Width] = bestRegion.width();
    result[JsonKeys::Height] = bestRegion.height();
    return result;
}

void ZoneAutoFiller::expandAdjacentZonesToFill(const QRectF& deletedGeom, const QVariantMap& adjacentZones)
{
    const qreal threshold = 0.02;

    QVariantList rightZones = adjacentZones[QStringLiteral("right")].toList();
    QVariantList leftZones = adjacentZones[QStringLiteral("left")].toList();
    QVariantList bottomZones = adjacentZones[QStringLiteral("bottom")].toList();
    QVariantList topZones = adjacentZones[QStringLiteral("top")].toList();

    // Try to expand right zones leftward
    for (const QVariant& rightZoneVar : rightZones) {
        QString rightZoneId = rightZoneVar.toString();
        auto zoneOpt = m_manager->getValidatedZone(rightZoneId);
        if (!zoneOpt) continue;

        QRectF rightRect = m_manager->extractZoneGeometry(*zoneOpt);

        if (rightRect.y() >= deletedGeom.y() - threshold
            && rightRect.bottom() <= deletedGeom.bottom() + threshold) {
            qreal expansion = rightRect.x() - deletedGeom.x();
            if (expansion > 0) {
                applyRelativeGeometry(rightZoneId, deletedGeom.x(), rightRect.y(),
                                      rightRect.width() + expansion, rightRect.height());
            }
        }
    }

    // Try to expand left zones rightward
    for (const QVariant& leftZoneVar : leftZones) {
        QString leftZoneId = leftZoneVar.toString();
        auto zoneOpt = m_manager->getValidatedZone(leftZoneId);
        if (!zoneOpt) continue;

        QRectF leftRect = m_manager->extractZoneGeometry(*zoneOpt);

        if (leftRect.y() >= deletedGeom.y() - threshold
            && leftRect.bottom() <= deletedGeom.bottom() + threshold) {
            qreal newRight = deletedGeom.right();
            qreal expansion = newRight - leftRect.right();
            QRectF testRect(leftRect.right(), leftRect.y(), expansion, leftRect.height());
            if (expansion > 0 && isRectangleEmpty(testRect, leftZoneId)) {
                applyRelativeGeometry(leftZoneId, leftRect.x(), leftRect.y(),
                                      leftRect.width() + expansion, leftRect.height());
            }
        }
    }

    // Try to expand bottom zones upward
    for (const QVariant& bottomZoneVar : bottomZones) {
        QString bottomZoneId = bottomZoneVar.toString();
        auto zoneOpt = m_manager->getValidatedZone(bottomZoneId);
        if (!zoneOpt) continue;

        QRectF bottomRect = m_manager->extractZoneGeometry(*zoneOpt);

        if (bottomRect.x() >= deletedGeom.x() - threshold
            && bottomRect.right() <= deletedGeom.right() + threshold) {
            qreal expansion = bottomRect.y() - deletedGeom.y();
            QRectF testRect(bottomRect.x(), deletedGeom.y(), bottomRect.width(), expansion);
            if (expansion > 0 && isRectangleEmpty(testRect, bottomZoneId)) {
                applyRelativeGeometry(bottomZoneId, bottomRect.x(), deletedGeom.y(),
                                      bottomRect.width(), bottomRect.height() + expansion);
            }
        }
    }

    // Try to expand top zones downward
    for (const QVariant& topZoneVar : topZones) {
        QString topZoneId = topZoneVar.toString();
        auto zoneOpt = m_manager->getValidatedZone(topZoneId);
        if (!zoneOpt) continue;

        QRectF topRect = m_manager->extractZoneGeometry(*zoneOpt);

        if (topRect.x() >= deletedGeom.x() - threshold
            && topRect.right() <= deletedGeom.right() + threshold) {
            qreal newBottom = deletedGeom.bottom();
            qreal expansion = newBottom - topRect.bottom();
            QRectF testRect(topRect.x(), topRect.bottom(), topRect.width(), expansion);
            if (expansion > 0 && isRectangleEmpty(testRect, topZoneId)) {
                applyRelativeGeometry(topZoneId, topRect.x(), topRect.y(),
                                      topRect.width(), topRect.height() + expansion);
            }
        }
    }
}

void ZoneAutoFiller::deleteZoneWithFill(const QString& zoneId, bool autoFill)
{
    auto zoneOpt = m_manager->getValidatedZone(zoneId);
    if (!zoneOpt) {
        qCWarning(lcEditorZone) << "Zone not found for deletion:" << zoneId;
        return;
    }

    QRectF deletedGeom = m_manager->extractZoneGeometry(*zoneOpt);
    QVariantMap adjacentZones;

    if (autoFill) {
        adjacentZones = findAdjacentZones(zoneId);
    }

    // Delete the zone
    m_manager->deleteZone(zoneId);

    // Auto-fill: expand adjacent zones to fill the gap
    if (autoFill) {
        expandAdjacentZonesToFill(deletedGeom, adjacentZones);
    }
}
