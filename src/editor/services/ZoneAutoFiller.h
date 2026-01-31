// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QObject>
#include <QRectF>
#include <QString>
#include <QVariantList>
#include <QVariantMap>

namespace PlasmaZones {

class ZoneManager;

/**
 * @brief Handles auto-fill and space-finding operations for zones
 *
 * Responsible for finding empty space in layouts, expanding zones to fill
 * available space, smart fill algorithms, and finding adjacent zones.
 * Delegates actual zone modifications back to ZoneManager.
 */
class ZoneAutoFiller : public QObject
{
    Q_OBJECT

public:
    explicit ZoneAutoFiller(ZoneManager* manager, QObject* parent = nullptr);

    /**
     * @brief Find zones adjacent to the given zone
     * @param zoneId The zone to find neighbors for
     * @param threshold Edge matching threshold (default 0.02 = 2%)
     * @return Map with "left", "right", "top", "bottom" lists of adjacent zone IDs
     */
    QVariantMap findAdjacentZones(const QString& zoneId, qreal threshold = 0.02) const;

    /**
     * @brief Expand a zone to fill available empty space around it
     * @param zoneId The zone to expand
     * @param mouseX Normalized mouse X position (0-1), or -1 to use zone center
     * @param mouseY Normalized mouse Y position (0-1), or -1 to use zone center
     * @return true if any expansion occurred
     */
    bool expandToFillSpace(const QString& zoneId, qreal mouseX = -1, qreal mouseY = -1);

    /**
     * @brief Calculate the fill region without applying it (for live preview)
     * @param zoneId The zone to calculate fill for (excluded from collision checks)
     * @param mouseX Normalized mouse X position (0-1)
     * @param mouseY Normalized mouse Y position (0-1)
     * @return QVariantMap with x, y, width, height of fill region, or empty if no valid region
     */
    QVariantMap calculateFillRegion(const QString& zoneId, qreal mouseX, qreal mouseY) const;

    /**
     * @brief Delete a zone and optionally expand neighbors to fill the gap
     * @param zoneId The zone to delete
     * @param autoFill If true, expand adjacent zones to fill the deleted zone's space
     */
    void deleteZoneWithFill(const QString& zoneId, bool autoFill = true);

    /**
     * @brief Check if a rectangle is empty (no zones occupy it)
     * @param rect The rectangle to check (0-1 normalized)
     * @param excludeZoneId Zone to exclude from check (optional)
     * @return true if the rectangle is empty
     */
    bool isRectangleEmpty(const QRectF& rect, const QString& excludeZoneId = QString()) const;

    /**
     * @brief Find the maximum extent a zone can expand in a given direction
     * @param zoneId The zone to expand
     * @param direction 0=left, 1=right, 2=up, 3=down
     * @return Maximum expansion amount (0-1 normalized), 0 if cannot expand
     */
    qreal findMaxExpansion(const QString& zoneId, int direction) const;

private:
    /**
     * @brief Smart fill: find the empty region at the target position and resize to fill it
     * @param zoneId The zone to fill
     * @param mouseX Normalized mouse X position (0-1), or -1 to use zone center
     * @param mouseY Normalized mouse Y position (0-1), or -1 to use zone center
     * @return true if zone was repositioned and resized
     */
    bool smartFillZone(const QString& zoneId, qreal mouseX = -1, qreal mouseY = -1);

    /**
     * @brief Find the largest empty rectangular region containing a target point
     * @param targetX Target X coordinate (0-1)
     * @param targetY Target Y coordinate (0-1)
     * @param excludeZoneIndex Zone index to exclude from collision checks
     * @return Best region as QRectF, or invalid rect if none found
     *
     * Used by both smartFillZone and calculateFillRegion.
     */
    QRectF findBestEmptyRegion(qreal targetX, qreal targetY, int excludeZoneIndex) const;

    /**
     * @brief Expand adjacent zones to fill space left by a deleted zone
     * @param deletedGeom The geometry of the deleted zone
     * @param adjacentZones Map of adjacent zone IDs by direction
     */
    void expandAdjacentZonesToFill(const QRectF& deletedGeom, const QVariantMap& adjacentZones);

    ZoneManager* m_manager;
};

} // namespace PlasmaZones
