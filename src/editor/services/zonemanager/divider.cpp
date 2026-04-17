// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "../ZoneManager.h"
#include "../../../core/constants.h"
#include "../../../core/logging.h"

#include <QtMath>

using namespace PlasmaZones;

QVariantList ZoneManager::getZonesSharingEdge(const QString& zoneId, qreal edgeX, qreal edgeY, qreal threshold)
{
    QVariantList result;

    int index = findZoneIndex(zoneId);
    if (index < 0 || index >= m_zones.size()) {
        qCDebug(lcEditorZone) << "PhosphorZones::Zone not found:" << zoneId;
        return result;
    }

    QVariantMap zone1 = m_zones[index].toMap();
    qreal z1x = zone1[::PhosphorZones::ZoneJsonKeys::X].toDouble();
    qreal z1y = zone1[::PhosphorZones::ZoneJsonKeys::Y].toDouble();
    qreal z1w = zone1[::PhosphorZones::ZoneJsonKeys::Width].toDouble();
    qreal z1h = zone1[::PhosphorZones::ZoneJsonKeys::Height].toDouble();

    bool checkRightEdge = qAbs(edgeX - (z1x + z1w)) < threshold;
    bool checkBottomEdge = qAbs(edgeY - (z1y + z1h)) < threshold;

    for (int i = 0; i < m_zones.size(); ++i) {
        if (i == index)
            continue;

        QVariantMap zone2 = m_zones[i].toMap();
        qreal z2x = zone2[::PhosphorZones::ZoneJsonKeys::X].toDouble();
        qreal z2y = zone2[::PhosphorZones::ZoneJsonKeys::Y].toDouble();
        qreal z2w = zone2[::PhosphorZones::ZoneJsonKeys::Width].toDouble();
        qreal z2h = zone2[::PhosphorZones::ZoneJsonKeys::Height].toDouble();

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
            zoneInfo[::PhosphorZones::ZoneJsonKeys::Id] = zone2[::PhosphorZones::ZoneJsonKeys::Id].toString();
            zoneInfo[::PhosphorZones::ZoneJsonKeys::X] = z2x;
            zoneInfo[::PhosphorZones::ZoneJsonKeys::Y] = z2y;
            zoneInfo[::PhosphorZones::ZoneJsonKeys::Width] = z2w;
            zoneInfo[::PhosphorZones::ZoneJsonKeys::Height] = z2h;
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

    qreal z1x = zone1[::PhosphorZones::ZoneJsonKeys::X].toDouble();
    qreal z1y = zone1[::PhosphorZones::ZoneJsonKeys::Y].toDouble();
    qreal z1w = zone1[::PhosphorZones::ZoneJsonKeys::Width].toDouble();
    qreal z2x = zone2[::PhosphorZones::ZoneJsonKeys::X].toDouble();
    qreal z2y = zone2[::PhosphorZones::ZoneJsonKeys::Y].toDouble();
    qreal z2h = zone2[::PhosphorZones::ZoneJsonKeys::Height].toDouble();

    qreal oldDividerPos = 0;
    const qreal threshold = EditorConstants::EdgeThreshold;

    if (isVertical) {
        if (z1x < z2x) {
            oldDividerPos = z1x + z1w;
        } else {
            oldDividerPos = z2x + zone2[::PhosphorZones::ZoneJsonKeys::Width].toDouble();
        }

        QList<int> leftZones;
        QList<int> rightZones;
        // Pre-allocate capacity (performance optimization)
        leftZones.reserve(m_zones.size() / 2);
        rightZones.reserve(m_zones.size() / 2);

        for (int i = 0; i < m_zones.size(); ++i) {
            const QVariantMap zone = m_zones[i].toMap();
            const qreal zx = zone[::PhosphorZones::ZoneJsonKeys::X].toDouble();
            const qreal zw = zone[::PhosphorZones::ZoneJsonKeys::Width].toDouble();
            const qreal rightEdge = zx + zw;

            if (qAbs(rightEdge - oldDividerPos) < threshold) {
                leftZones.append(i);
            } else if (qAbs(zx - oldDividerPos) < threshold) {
                rightZones.append(i);
            }
        }

        for (int idx : leftZones) {
            const QVariantMap zone = m_zones[idx].toMap();
            const QString zoneId = zone[::PhosphorZones::ZoneJsonKeys::Id].toString();
            const qreal x = zone[::PhosphorZones::ZoneJsonKeys::X].toDouble();
            const qreal y = zone[::PhosphorZones::ZoneJsonKeys::Y].toDouble();
            const qreal w = zone[::PhosphorZones::ZoneJsonKeys::Width].toDouble();
            const qreal h = zone[::PhosphorZones::ZoneJsonKeys::Height].toDouble();
            result.append(qMakePair(zoneId, QRectF(x, y, w, h)));
        }
        for (int idx : rightZones) {
            const QVariantMap zone = m_zones[idx].toMap();
            const QString zoneId = zone[::PhosphorZones::ZoneJsonKeys::Id].toString();
            const qreal x = zone[::PhosphorZones::ZoneJsonKeys::X].toDouble();
            const qreal y = zone[::PhosphorZones::ZoneJsonKeys::Y].toDouble();
            const qreal w = zone[::PhosphorZones::ZoneJsonKeys::Width].toDouble();
            const qreal h = zone[::PhosphorZones::ZoneJsonKeys::Height].toDouble();
            result.append(qMakePair(zoneId, QRectF(x, y, w, h)));
        }
    } else {
        if (z1y < z2y) {
            oldDividerPos = z1y + zone1[::PhosphorZones::ZoneJsonKeys::Height].toDouble();
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
            const qreal zy = zone[::PhosphorZones::ZoneJsonKeys::Y].toDouble();
            const qreal zh = zone[::PhosphorZones::ZoneJsonKeys::Height].toDouble();
            const qreal bottomEdge = zy + zh;

            if (qAbs(bottomEdge - oldDividerPos) < threshold) {
                topZones.append(i);
            } else if (qAbs(zy - oldDividerPos) < threshold) {
                bottomZones.append(i);
            }
        }

        for (int idx : topZones) {
            const QVariantMap zone = m_zones[idx].toMap();
            const QString zoneId = zone[::PhosphorZones::ZoneJsonKeys::Id].toString();
            const qreal x = zone[::PhosphorZones::ZoneJsonKeys::X].toDouble();
            const qreal y = zone[::PhosphorZones::ZoneJsonKeys::Y].toDouble();
            const qreal w = zone[::PhosphorZones::ZoneJsonKeys::Width].toDouble();
            const qreal h = zone[::PhosphorZones::ZoneJsonKeys::Height].toDouble();
            result.append(qMakePair(zoneId, QRectF(x, y, w, h)));
        }
        for (int idx : bottomZones) {
            const QVariantMap zone = m_zones[idx].toMap();
            const QString zoneId = zone[::PhosphorZones::ZoneJsonKeys::Id].toString();
            const qreal x = zone[::PhosphorZones::ZoneJsonKeys::X].toDouble();
            const qreal y = zone[::PhosphorZones::ZoneJsonKeys::Y].toDouble();
            const qreal w = zone[::PhosphorZones::ZoneJsonKeys::Width].toDouble();
            const qreal h = zone[::PhosphorZones::ZoneJsonKeys::Height].toDouble();
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

    qreal z1x = zone1[::PhosphorZones::ZoneJsonKeys::X].toDouble();
    qreal z1y = zone1[::PhosphorZones::ZoneJsonKeys::Y].toDouble();
    qreal z1w = zone1[::PhosphorZones::ZoneJsonKeys::Width].toDouble();
    qreal z2x = zone2[::PhosphorZones::ZoneJsonKeys::X].toDouble();
    qreal z2y = zone2[::PhosphorZones::ZoneJsonKeys::Y].toDouble();
    qreal z2h = zone2[::PhosphorZones::ZoneJsonKeys::Height].toDouble();

    qreal oldDividerPos = 0;
    qreal threshold = EditorConstants::EdgeThreshold;

    if (isVertical) {
        if (z1x < z2x) {
            oldDividerPos = z1x + z1w;
        } else {
            oldDividerPos = z2x + zone2[::PhosphorZones::ZoneJsonKeys::Width].toDouble();
        }

        QList<int> leftZones;
        QList<int> rightZones;
        // Pre-allocate capacity (performance optimization)
        leftZones.reserve(m_zones.size() / 2);
        rightZones.reserve(m_zones.size() / 2);

        for (int i = 0; i < m_zones.size(); ++i) {
            const QVariantMap zone = m_zones[i].toMap();
            const qreal zx = zone[::PhosphorZones::ZoneJsonKeys::X].toDouble();
            const qreal zw = zone[::PhosphorZones::ZoneJsonKeys::Width].toDouble();
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
            qreal newWidth = zone[::PhosphorZones::ZoneJsonKeys::Width].toDouble() + deltaX;
            if (newWidth < minSize) {
                valid = false;
                break;
            }
            qreal zx = zone[::PhosphorZones::ZoneJsonKeys::X].toDouble();
            if (zx + newWidth > 1.0) {
                valid = false;
                break;
            }
        }

        if (valid) {
            for (int idx : rightZones) {
                QVariantMap zone = m_zones[idx].toMap();
                qreal zx = zone[::PhosphorZones::ZoneJsonKeys::X].toDouble();
                qreal zw = zone[::PhosphorZones::ZoneJsonKeys::Width].toDouble();
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
            QString zoneId = zone[::PhosphorZones::ZoneJsonKeys::Id].toString();
            qreal zx = zone[::PhosphorZones::ZoneJsonKeys::X].toDouble();
            qreal newWidth = newDividerX - zx;

            if (newWidth < minSize) {
                newWidth = minSize;
            }

            zone[::PhosphorZones::ZoneJsonKeys::Width] = newWidth;
            if (isFixedMode(zone)) {
                syncFixedFromRelative(zone);
            }
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
            QString zoneId = zone[::PhosphorZones::ZoneJsonKeys::Id].toString();
            qreal oldX = zone[::PhosphorZones::ZoneJsonKeys::X].toDouble();
            qreal oldW = zone[::PhosphorZones::ZoneJsonKeys::Width].toDouble();
            qreal newX = newDividerX;
            qreal newWidth = (oldX + oldW) - newX;

            if (newWidth < minSize) {
                newWidth = minSize;
                newX = (oldX + oldW) - minSize;
            }

            zone[::PhosphorZones::ZoneJsonKeys::X] = newX;
            zone[::PhosphorZones::ZoneJsonKeys::Width] = newWidth;
            if (isFixedMode(zone)) {
                syncFixedFromRelative(zone);
            }
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
            oldDividerPos = z1y + zone1[::PhosphorZones::ZoneJsonKeys::Height].toDouble();
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
            const qreal zy = zone[::PhosphorZones::ZoneJsonKeys::Y].toDouble();
            const qreal zh = zone[::PhosphorZones::ZoneJsonKeys::Height].toDouble();
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
            qreal newHeight = zone[::PhosphorZones::ZoneJsonKeys::Height].toDouble() + deltaY;
            if (newHeight < minSize) {
                valid = false;
                break;
            }
            qreal zy = zone[::PhosphorZones::ZoneJsonKeys::Y].toDouble();
            if (zy + newHeight > 1.0) {
                valid = false;
                break;
            }
        }

        if (valid) {
            for (int idx : bottomZones) {
                QVariantMap zone = m_zones[idx].toMap();
                qreal zy = zone[::PhosphorZones::ZoneJsonKeys::Y].toDouble();
                qreal zh = zone[::PhosphorZones::ZoneJsonKeys::Height].toDouble();
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
            QString zoneId = zone[::PhosphorZones::ZoneJsonKeys::Id].toString();
            qreal zy = zone[::PhosphorZones::ZoneJsonKeys::Y].toDouble();
            qreal newHeight = newDividerY - zy;

            if (newHeight < minSize) {
                newHeight = minSize;
            }

            zone[::PhosphorZones::ZoneJsonKeys::Height] = newHeight;
            if (isFixedMode(zone)) {
                syncFixedFromRelative(zone);
            }
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
            QString zoneId = zone[::PhosphorZones::ZoneJsonKeys::Id].toString();
            qreal oldY = zone[::PhosphorZones::ZoneJsonKeys::Y].toDouble();
            qreal oldH = zone[::PhosphorZones::ZoneJsonKeys::Height].toDouble();
            qreal newY = newDividerY;
            qreal newHeight = (oldY + oldH) - newY;

            if (newHeight < minSize) {
                newHeight = minSize;
                newY = (oldY + oldH) - minSize;
            }

            zone[::PhosphorZones::ZoneJsonKeys::Y] = newY;
            zone[::PhosphorZones::ZoneJsonKeys::Height] = newHeight;
            if (isFixedMode(zone)) {
                syncFixedFromRelative(zone);
            }
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
